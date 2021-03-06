#include <iostream>
#include <cmath>
#include <vector>
#include <Eigen/Dense>
#include <string>
#include <map>
#include "ReadInput.h"
#include <Eigen/Sparse>
#include <fstream>
#include <Eigen/Eigenvalues>
#include <ctime>
#include <queue>
#include <algorithm>
#include <iomanip>

#include "Functions.h"

void BuildFockMatrix(Eigen::MatrixXd &FockMatrix, Eigen::MatrixXd &DensityMatrix, std::map<std::string, double> &Integrals, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, int NumElectrons);
void BuildFockMatrix(Eigen::MatrixXd &FockMatrix, Eigen::MatrixXd &DensityMatrix, Eigen::MatrixXd &OppositeSpinDensity, std::map<std::string, double> &Integrals, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, int NumElectrons);
double Metric(int NumElectrons, Eigen::MatrixXd &FirstDensityMatrix, Eigen::MatrixXd &SecondDensityMatrix);
void ModifyBias(std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, short int WhichSoln);
void NewDensityMatrix(Eigen::MatrixXd &DensityMatrix, Eigen::MatrixXd &CoeffMatrix, std::vector<int> OccupiedOrbitals, std::vector<int> VirtualOrbitals);
void BuildFockMatrix(Eigen::MatrixXd &FockMatrix, Eigen::MatrixXd &HCore, Eigen::MatrixXd &DensityImp, Eigen::MatrixXd &RotationMatrix, InputObj &Input, double &ChemicalPotential, int FragmentIndex);
void GetCASPos(InputObj Input, int FragmentIndex, std::vector< int > &FragmentPos, std::vector< int > &BathPos);

/// <summary>
/// This generates a positive, random density matrix and then divides by the norm. I know that
/// the actual density matrix should have a trace of NumElectrons, and not 1, but this works 
/// well enough for now. Note that this is not symmetric. This is because whenever I use the 
/// density matrix, I only call the upper triangle, so it doesn't have to be symmetric here, 
/// it will be as if it were symmetric in calculations.
/// </summary>
/// <param name="DensityMatrix">
/// Density matrix to be randomized. The new density matrix is stored here.
/// </param>
void GenerateRandomDensity(Eigen::MatrixXd &DensityMatrix)
{
    for(int i = 0; i < DensityMatrix.rows(); i++)
    {
        for(int j = i; j < DensityMatrix.cols(); j++)
        {
            DensityMatrix(i, j) = rand() / RAND_MAX;
            DensityMatrix(j, i) = DensityMatrix(i, j);
        }
    }
    DensityMatrix / DensityMatrix.trace();
}

// This generates a random density matrix where the diagonals are 1 and the off diagonals are translationally symmetric.
void GenerateRandomDensityTS(Eigen::MatrixXd &DensityMatrix)
{
    // Generate a list of random elements to be put into the off diagonal.
    std::vector<double> RandomElement;
    for (int i = 0; i < DensityMatrix.rows(); i++)
    {
        RandomElement.push_back(rand() / RAND_MAX);
    }
    for (int i = 0; i < DensityMatrix.rows(); i++)
    {
        DensityMatrix(i, i) = 1.0;
        for (int j = i + 1; j < DensityMatrix.cols(); j++)
        {
            DensityMatrix(i, j) = RandomElement[j];
            DensityMatrix(j, i) = DensityMatrix.coeffRef(i, j);
        }
    }
}

/// <summary>
/// Sum of element wise product of two matrices. I think Eigen has an implementation for this, so I should
/// try to remove this function.
/// </summary>
/// <param name="FirstMatrix">
/// First matrix in the dot product.
/// </param>
/// <param name="SecondMatrix">
/// Second matrix in the dot product.
/// </param>
double MatrixDot(Eigen::MatrixXd FirstMatrix, Eigen::MatrixXd SecondMatrix)
{
    double Dot = 0;
    for(int i = 0; i < FirstMatrix.rows(); i++)
    {
        for(int j = 0; j < FirstMatrix.cols(); j++)
        {
            Dot += FirstMatrix(i, j) * SecondMatrix(i, j);
        }
    }
    return Dot;
}

/// <summary>
/// Forms the DIIS Fock matrix and puts it into FockMatrix.
/// </summary>
/// <param name="FockMatrix">
/// Will hold F', the modified Fock matrix.
/// </param>
/// <param name="AllFockMatrices">
/// Vector of all previous Fock matrices to be used in the sum that gives F'
/// </param>
/// <param name="AllErrorMatrices">
/// Vector of error matrices, to be used to form the linear system.
/// </param>
void DIIS(Eigen::MatrixXd &FockMatrix, std::vector< Eigen::MatrixXd > &AllFockMatrices, std::vector< Eigen::MatrixXd > &AllErrorMatrices)
{
    Eigen::MatrixXd B(AllErrorMatrices.size() + 1, AllErrorMatrices.size() + 1); // This is the linear system we solve for the coefficients.
    for(int i = 0; i < AllErrorMatrices.size(); i++)
    {
        for(int j = i; j < AllErrorMatrices.size(); j++)
        {
            B(i, j) = MatrixDot(AllErrorMatrices[i], AllErrorMatrices[j]);
            B(j, i) = B(i, j);
        }
    }
    for(int i = 0; i < AllErrorMatrices.size(); i++)
    {
        B(i, AllErrorMatrices.size()) = -1;
        B(AllErrorMatrices.size(), i) = -1;
    }
    B(AllErrorMatrices.size(), AllErrorMatrices.size()) = 0;

    Eigen::VectorXd b = Eigen::VectorXd::Zero(AllErrorMatrices.size() + 1);
    b(AllErrorMatrices.size()) = -1;
    // We want to solve for c in Bc = b
    Eigen::VectorXd c = B.colPivHouseholderQr().solve(b);

    /* And now put it together to get the new FockMatrix. If we are on the first iteration, then c1 = 1 and we end up where we
       started. */
    FockMatrix = Eigen::MatrixXd::Zero(FockMatrix.rows(), FockMatrix.cols());
    for(int i = 0; i < AllFockMatrices.size(); i++)
    {
        FockMatrix += c[i] * AllFockMatrices[i];
    }
}

/// <summary>
/// Does MOM to determine the orbitals with maximum overlap, and sets these as the
/// occupied orbitals.
/// </summary>
/// <param name="DensityMatrix">
/// Density matrix of the current iteration.
/// </param>
/// <param name="CoeffMatrix">
/// Coefficient matrix of the current iteration. Used to calculate O.
/// </param>
/// <param name="CoeffMatrixPrev">
/// Coefficient matrix of the previous iteration. Used to calculate O.
/// </param>
/// <param name="OverlapMatrix">
/// Overlap matrix, S. Used to calculate O.
/// </param>
/// <param name="NumOcc">
/// Number of occupied orbitals. Will store new occupied orbitals.
/// </param>
/// <param name="NumAO">
/// The total size of the basis. Used to scan over list of orbitals and determine which ones are virtual.
/// </param>
/// <param name="OccupiedOrbitals">
/// Stores the occupied orbitals.
/// </param>
/// <param name="VirtualOrbitals">
/// Stores the virtual orbitals.
/// </param>
void MaximumOverlapMethod(Eigen::MatrixXd &DensityMatrix, Eigen::MatrixXd &CoeffMatrix, Eigen::MatrixXd &CoeffMatrixPrev, Eigen::MatrixXd &OverlapMatrix, int NumOcc, int NumAO, std::vector<int> &OccupiedOrbitals, std::vector<int> &VirtualOrbitals) // MoM 
{
    /* This holds the set of total summed overlaps described below. It is a queue because this is an efficient way to choose largest elements in a set, at
       the cost of more complex insertion. The first in the pair is the value of the summed overlap and the second index is the orbital number, which ranges
       over all orbitals in the new coefficient matrix, denoted j below. */
    std::priority_queue< std::pair<double, int> > PQueue;

    /* Now we calculate the projections for the j'th new orbital in the new coefficient matrix. This is the j'th column in CoeffMatrix. 
       What we do is calculate the total summed overlap of EVERY new molecular orbital with every OCCUPIED molecular orbital in the
       old coefficient matrix, and then we take the NumOcc highest new orbitals to be the new overlap orbitals. */
    for(int j = 0; j < CoeffMatrix.cols(); j++) // Loop over EVERY orbital in the new coefficient matrix.
    {
        double NewOrbitalTotalOverlap = 0; // Holds the sum of < jthNewOrbital | OldOccupiedOrbital > over all OldOccupiedOrbital
        for(int nu = 0; nu < CoeffMatrix.rows(); nu++) // nu and mu index the basis functions, so they run down the coefficient matrix and label rows. This is needed since basis functions may not be orthogonal.
        {
            for(int mu = 0; mu < CoeffMatrix.rows(); mu++) // See previous comment.
            {
                for(int i = 0; i < OccupiedOrbitals.size(); i++) // i runs over all occupied orbitals in the old coefficient matrix.
                {
                    // Note that I am calling the column index first, so it may look like the matrix is transposed even though it's standard.
                    // This is C_mu,i(old) * S_mu,nu * C_nu,j(new) where i is an occupied orbital. For each j, we sum over mu and nu. If S is the identity
                    // matrix, then for each j we are calculating sum_mu C_mu,i(old) * C_nu,j(new), or C_i(old) dot C_j(new)
                    NewOrbitalTotalOverlap += (CoeffMatrixPrev.col(OccupiedOrbitals[i])[mu] * OverlapMatrix(mu, nu) * CoeffMatrix.col(j)[nu]); 
                }
            }
        }
        PQueue.push(std::pair<double, int>(NewOrbitalTotalOverlap, j)); 
        // Adds the value of the summed overlap to the queue.
        // The first value is the value of the overlap, which is used to order the list. The second value is the orbital number,
        // which is what we actually care about, and it will be the value we pull out of this queue.
    } // end loop over new orbitals j.
   
    /* Now we take the NumOcc highest summed overlap values and get their corresponding orbitals, adding them to the list of occupied
       orbitals. This is then used to calculate the density matrix and used in the MOM procedure for the next step. In order to this,
       we search for the highest element in PQueue using top(). Then we remove this element using pop(). Then top() gives the next 
       highest element the next time we call it. */
    for(int i = 0; i < NumOcc; i++)
    {
        OccupiedOrbitals[NumOcc - 1 - i] = PQueue.top().second; // Insert largest indices into occupied orbital list, smallest first.
        PQueue.pop(); // Remove largest element. The next time we call top(), we will get the next highest element.
    }
    int VirtIndex = 0;
    for(int i = 0; i < NumAO; i++) // Determine which orbitals are not in the occupied orbitals list and add them to the virtual orbitals list.
    {
        if(std::find(OccupiedOrbitals.begin(), OccupiedOrbitals.end(), i) == OccupiedOrbitals.end()) // Means not in occupied orbital list.
        {
            VirtualOrbitals[VirtIndex] = i;
            VirtIndex++;
        }
    }

    /* This calculates the density matrix (P) using P = C(occ) * C(occ)^T */
	for (int i = 0; i < DensityMatrix.rows(); i++)
	{
		for (int j = 0; j < DensityMatrix.cols(); j++)
		{
			double DensityElement = 0;
			for (int k = 0; k < NumOcc; k++)
			{
				DensityElement += CoeffMatrix(i, OccupiedOrbitals[k]) * CoeffMatrix(j, OccupiedOrbitals[k]);
			}
			DensityMatrix(i, j) = DensityElement;
		}
    }
}

/// <summary>
/// Calculates the squared norm of two matrices. Used to check if density matrix is converged.
/// Eigen has an implementation for this, so I should remove this.
/// </summary>
/// <param name="DensityMatrix">
/// Density matrix of the current iteration.
/// </param>
/// <param name="DensitymatrixPrev">
/// Density matrix of the previous iteration.
/// </param>
double CalcDensityRMS(Eigen::MatrixXd &DensityMatrix, Eigen::MatrixXd &DensityMatrixPrev)
{
    double DensityRMS = 0;
    for(int i = 0; i < DensityMatrix.rows(); i++)
    {
        for(int j = 0; j < DensityMatrix.cols(); j++)
        {
            DensityRMS += (DensityMatrix(i, j) - DensityMatrixPrev(i, j)) * (DensityMatrix(i, j) - DensityMatrixPrev(i, j));
        }
    }
    return DensityRMS;
}

/* Calculates the sum of the squared elements of Matrix. Note that this is not actually RMS, I just use it as an error. */
double CalcMatrixRMS(Eigen::MatrixXd &Matrix)
{
    double MatrixRMS = 0;
    for(int i = 0; i < Matrix.rows(); i++)
    {
        for(int j = 0; j < Matrix.cols(); j++)
        {
            MatrixRMS += Matrix(i, j) * Matrix(i, j);
        }
    }
    return MatrixRMS;
}

/// <summary>
/// One SCF iteration. Takes a density matrix, generates the corresponding Fock matrix, and then computes a new density
/// matrix and the corresponding energy. The energy is returned.
/// </summary>
/// <param name="DensityMatrix">
/// Density matrix of the current iteration.
/// </param>
/// <param name="Input">
/// Object containing all input parameters. Includes options, integrals, and number of electrons/orbitals.
/// </param>
/// <param name="HCore">
/// Fock matrix generate from a zero density matrix. Stored because it can be reused for the generation of a new fock
/// matrix and of the energy.
/// </param>
/// <param name="Bias">
/// List of all biases. Used when for SCF metadynamics, but just does SCF regularly when this is empty.
/// </param>
/// <param name="CoeffMatrix">
/// Stores the current density matrix to be used to calculate new density matrix. This needs to be stored because
/// it is also used for MOM.
/// </param>
/// <param name="AllFockMatrices">
/// Stores all previous fock matrices and is used for DIIS.
/// </param>
/// <param name="AllErrorMatrices">
/// Stores all previous error matrices and is used for DIIS.
/// </param>
/// <param name="CoeffMatrixPrev">
/// Coefficient matrix of the previous iteration. Used for MOM.
/// </param>
/// <param name="OccupiedOrbitals">
/// Holds occupied orbitals. If this is the first SCF loop, it is modified in MOM. If 
/// this is the second SCF loop, it is fixed and just used to calculate the density matrix.
/// </param>
/// <param name="VirtualOrbitals">
/// Holds virtual orbitals. I don't remember why I need this information...
/// </param>
double SCFIteration(Eigen::MatrixXd &DensityMatrix, InputObj &Input, Eigen::MatrixXd &HCore, Eigen::MatrixXd &SOrtho, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, Eigen::MatrixXd &CoeffMatrix, std::vector< Eigen::MatrixXd > &AllFockMatrices, std::vector< Eigen::MatrixXd > &AllErrorMatrices, Eigen::MatrixXd &CoeffMatrixPrev, std::vector<int> &OccupiedOrbitals, std::vector<int> &VirtualOrbitals)
{
    Eigen::MatrixXd FockMatrix(DensityMatrix.rows(), DensityMatrix.cols()); // This will hold the FockMatrix.
    BuildFockMatrix(FockMatrix, DensityMatrix, Input.Integrals, Bias, Input.NumElectrons); // Calculates and stores fock matrix. Includes bias.
    AllFockMatrices.push_back(FockMatrix); // Store this iteration's Fock matrix for the DIIS procedure.
    
    Eigen::MatrixXd ErrorMatrix = FockMatrix * DensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * DensityMatrix * FockMatrix; // DIIS error matrix of the current iteration: FPS - SPF
    AllErrorMatrices.push_back(ErrorMatrix); // Save error matrix for DIIS.
    if (Input.Options[0]) // Means use DIIS
    {
        DIIS(FockMatrix, AllFockMatrices, AllErrorMatrices); // Generates F' using DIIS and stores it in FockMatrix.
    } // If DIIS isn't being used, nothing needs to be done.

    if (AllFockMatrices.size() >= 5) // We only want about 5 Fock matrices for DIIS, so we'll throw away the earlier ones.
	{
		AllFockMatrices.erase(AllFockMatrices.begin(), AllFockMatrices.begin() + 1);
		AllErrorMatrices.erase(AllErrorMatrices.begin(), AllErrorMatrices.begin() + 1);
    }

    Eigen::MatrixXd FockOrtho = SOrtho.transpose() * FockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > EigensystemFockOrtho(FockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    CoeffMatrix = SOrtho * EigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.

	/* Density matrix: C(occ) * C(occ)^T */
    if(Input.Options[1]) // Means use MOM
    {
        if(!Bias.empty()) // Means the first SCP loop when there is a bias. Use MOM for this loop.
        {
            MaximumOverlapMethod(DensityMatrix, CoeffMatrix, CoeffMatrixPrev, Input.OverlapMatrix, Input.NumOcc, Input.NumAO, OccupiedOrbitals, VirtualOrbitals); // MoM 
            CoeffMatrixPrev = CoeffMatrix; // Now that we finish the MoM iteration, set CoeffMatrixPrev.
        }
        else // Then remove the bias and lock in the orbitals.
        {
           for (int i = 0; i < DensityMatrix.rows(); i++)
           {
                for (int j = 0; j < DensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.NumOcc; k++)
                    {
                        DensityElement += CoeffMatrix(i, OccupiedOrbitals[k]) * CoeffMatrix(j, OccupiedOrbitals[k]);
                    }
                    DensityMatrix(i, j) = DensityElement;
                }
            }
        }
    }
    else // Means do not use MOM
    {
        for (int i = 0; i < DensityMatrix.rows(); i++)
        {
            for (int j = 0; j < DensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.NumOcc; k++)
                {
                    DensityElement += CoeffMatrix(i, OccupiedOrbitals[k]) * CoeffMatrix(j, OccupiedOrbitals[k]);
                }
                DensityMatrix(i, j) = DensityElement;
            }
        }
    }

	/* Now calculate the HF energy. E = sum_ij P_ij * (HCore_ij + F_ij) */
    double Energy = (DensityMatrix.cwiseProduct(HCore + FockMatrix)).sum();
    return Energy;
}

/// <summary>
/// Runs the SCF method until convergence.
/// </summary>
/// <param name="Bias">
/// List of all biases. Used when for SCF metadynamics in the first SCF loop.
/// </param>
/// <param name="SolnNum">
/// The number solution we are looking for. Just helps with organizing the output.
/// </param>
/// <param name="DensityMatrix">
/// Stores density matrix. Will be needed after convergence to add to list of biases.
/// </param>
/// <param name="Input">
/// Object containing all input parameters. Includes options, integrals, and number of electrons/orbitals.
/// </param>
/// <param name="Output">
/// Output stream that we write to.
/// </param>
/// <param name="SOrtho">
/// S^(-1/2). Used to put everything in an orthogonal basis.
/// </param>
/// <param name="HCore">
/// Fock matrix generate from a zero density matrix. Stored because it can be reused for the generation of a new fock
/// matrix and of the energy.
/// </param>
/// <param name="AllEnergies">
/// Vector of all previous energies. Used to determine if solution is unique.
/// </param>
/// <param name="CoeffMatrix">
/// Stores the current density matrix to be used to calculate new density matrix. This needs to be stored because
/// it is used for generating the new density matrix. I don't think I need to pass it as an argument though...
/// </param>
/// <param name="OccupiedOrbitals">
/// List of all occupied orbitals. This is modifed if MOM is used, but we need it to be fixed and used to calculated
/// the density matrix when MOM is not used.
/// </param>
/// <param name="VirtualOrbitals">
/// List of all virtual orbitals.
/// </param>
/// <param name="SCFCount">
/// Counts how many SCF iterations have been performed. This is carried through all SCF loops in the program.
/// </param>
/// <param name="MaxSCF">
/// Search is terminated when SCFCount exceeds this.
/// </param>
double SCF(std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, int SolnNum, Eigen::MatrixXd &DensityMatrix, InputObj &Input, std::ofstream &Output, Eigen::MatrixXd &SOrtho, Eigen::MatrixXd &HCore, std::vector< double > &AllEnergies, Eigen::MatrixXd &CoeffMatrix, std::vector<int> &OccupiedOrbitals, std::vector<int> &VirtualOrbitals, int &SCFCount, int MaxSCF)
{
	double SCFTol = 1E-8; // SCF will terminate when the DIIS error is below this amount. 
    std::cout << std::fixed << std::setprecision(10);

	Output << "Beginning search for Solution " << SolnNum << std::endl;
	Output << "Iteration\tEnergy" << std::endl;
	std::cout << "SCF MetaD: Beginning search for Solution " << SolnNum << std::endl;
	clock_t ClockStart = clock();

    double Energy = 1; // HF energy of SCF iteration.
    double DIISError = 1; // Square sum of DIIS error matrix of the current iteration. Used to test convergence.
    Eigen::MatrixXd DensityMatrixPrev; // Stores density matrix of the previous iteration to test density matrix convergence.
    double EnergyPrev = 1; // Stores energy of previous iteration to test energy convergence.
    double DensityRMS = 1; // Stores squared different between two sequential density matrices.
    unsigned short int Count = 1; // Counts number of iterations.
    bool isUniqueSoln = false; // Will tell us if the solution is unique by checking against all previous energies.
    bool ContinueSCF = true; // Tells us when SCF is converged, based on whatever criterion is selected.
    
    while(!isUniqueSoln)
    {
        std::vector< Eigen::MatrixXd > AllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > AllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd CoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        ContinueSCF = true;
        Count = 1;
        while((ContinueSCF || Count < 5) && !Bias.empty()) // Do 15 times atleast, but skip if this is the first SCF.
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                DensityMatrixPrev = DensityMatrix;
            }
            Energy = SCFIteration(DensityMatrix, Input, HCore, SOrtho, Bias, CoeffMatrix, AllFockMatrices, AllErrorMatrices, CoeffMatrixPrev, OccupiedOrbitals, VirtualOrbitals);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (DensityMatrix - DensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (DensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(AllErrorMatrices[AllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with a biased energy of " << Energy + Input.Integrals["0 0 0 0"];
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl; // I planned to list the energy of each iteration, but there is a ridiculous amount of iterations.
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            /* This is a work-around that I put in. The first guess of the density is a zero matrix and this is not good. Unfortunately, DIIS
               rarely corrects this so I find that it helps to clear the Fock and Error matrices after a few iterations and we have a more reasonable
               guess of the coefficient, and thus density, matrices. Then DIIS converges to a reasonable solution. */
            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 200 == 0) // Shouldn't take this long.
            {
                AllFockMatrices.clear();
                AllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        } // Means we have converged with the bias. Now we remove the bias and converge to the minimum

        Count = 1;
        std::vector< std::tuple< Eigen::MatrixXd, double, double > > EmptyBias; // Same type as Bias, but it's empty so it's the same as having no bias.
        AllFockMatrices.clear();
        AllErrorMatrices.clear();
        ContinueSCF = true; // Reset for the next loop to start.

        while(ContinueSCF || Count < 5)
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                DensityMatrixPrev = DensityMatrix;
            }
            Energy = SCFIteration(DensityMatrix, Input, HCore, SOrtho, EmptyBias, CoeffMatrix, AllFockMatrices, AllErrorMatrices, CoeffMatrixPrev, OccupiedOrbitals, VirtualOrbitals);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (DensityMatrix - DensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (DensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = AllErrorMatrices[AllErrorMatrices.size() - 1].squaredNorm();
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with an energy of " << Energy + Input.Integrals["0 0 0 0"];//  << " and DIIS error of " << DIISError << std::endl;
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl;
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 200 == 0)
            {
                AllFockMatrices.clear();
                AllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                // DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
            }
        }

        isUniqueSoln = true;
        short int WhichSoln = -1; // If we found a solution we already visited, this will mark which of the previous solutions we are at.
        if(Energy + Input.Integrals["0 0 0 0"] > 0) // Hopefully we won't be dissociating.
        {
            isUniqueSoln = false;
        }
        else
        {
            for(int i = 0; i < AllEnergies.size(); i++) // Compare energy with previous solutions.
            {
                if(fabs(Energy + Input.Integrals["0 0 0 0"] - AllEnergies[i]) < 1E-5) // Checks to see if new energy is equal to any previous energy.
                {
                    isUniqueSoln = false; // If it matches at one, set this flag to false so the SCF procedure can repeat for this solution.
                    WhichSoln = i;
                    break;
                }
            }
            if(isUniqueSoln) // If it still looks good
            {
                for(int i = 0; i < Bias.size(); i++)
                {
                    if((DensityMatrix - std::get<0>(Bias[i])).squaredNorm() < 1E-3) // Means same density matrix as found before
                    {
                        isUniqueSoln = false;
                        WhichSoln = i;
                        break;
                    }
                }
            }
        }

        if(!isUniqueSoln) // If the flag is still false, we modify the bias and hope that this gives a better result.
        {
            std::cout << "SCF MetaD: Solution is not unique. Retrying solution " << SolnNum << "." << std::endl;
            ModifyBias(Bias, WhichSoln); // Changes bias, usually means increase value of parameters.

            /* We should also change the density matrix to converge to different solution, but it is not
               obvious how we should do that. We could rotate two orbitals, but this may not be enough to
               find a different solution. We could randomize the density matrix, but then we get 
               unphysical results. */
            if(Input.DensityOption == 0) NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
            if(Input.DensityOption == 1) GenerateRandomDensity(DensityMatrix);
            if(Input.DensityOption == 2) DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
            if(Input.DensityOption == 3) GenerateRandomDensityTS(DensityMatrix);
        }
    }

    AllEnergies.push_back(Energy + Input.Integrals["0 0 0 0"]);

	std::cout << "SCF MetaD: Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
	std::cout << "SCF MetaD: This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;
	Output << "Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
    Output << "and orbitals:" << std::endl;
    Output << "Basis\tMolecular Orbitals" << std::endl;
    for(int mu = 0; mu < CoeffMatrix.rows(); mu++)
    {
        Output << mu + 1;
        for(int i = 0; i < OccupiedOrbitals.size(); i++) // Loop through each molecular orbital
        {
            Output << "\t" << CoeffMatrix(mu, OccupiedOrbitals[i]); // Select the columns corresponding to the occupied orbitals.
        }
        Output << "\n";
    }
    Output << "And Density Matrix:\n" << DensityMatrix << std::endl;
    
	Output << "This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;

    return Energy + Input.Integrals["0 0 0 0"];
}

/******* OVERLOADED FUNCTION FOR FULL SYSTEM PLUS CORRELATION ********/
double SCFIteration(Eigen::MatrixXd &DensityMatrix, InputObj &Input, Eigen::MatrixXd &HCore, Eigen::MatrixXd &SOrtho, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, Eigen::MatrixXd &CoeffMatrix, std::vector< Eigen::MatrixXd > &AllFockMatrices, std::vector< Eigen::MatrixXd > &AllErrorMatrices, Eigen::MatrixXd &CoeffMatrixPrev, std::vector<int> &OccupiedOrbitals, std::vector<int> &VirtualOrbitals, Eigen::MatrixXd DMETPotential, Eigen::VectorXd &OrbitalEV)
{
    Eigen::MatrixXd FockMatrix(DensityMatrix.rows(), DensityMatrix.cols()); // This will hold the FockMatrix.
    BuildFockMatrix(FockMatrix, DensityMatrix, Input.Integrals, Bias, Input.NumElectrons); // Calculates and stores fock matrix. Includes bias.
    FockMatrix += DMETPotential; // If before DIIS, results in improper Fock matrix. If after DIIS, gradient is wrong.
    AllFockMatrices.push_back(FockMatrix); // Store this iteration's Fock matrix for the DIIS procedure.
    // std::cout << "F\n" << FockMatrix << std::endl;
    // std::cout << "D\n" << DensityMatrix << std::endl;

    Eigen::MatrixXd ErrorMatrix = FockMatrix * DensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * DensityMatrix * FockMatrix; // DIIS error matrix of the current iteration: FPS - SPF
    AllErrorMatrices.push_back(ErrorMatrix); // Save error matrix for DIIS.
    DIIS(FockMatrix, AllFockMatrices, AllErrorMatrices); // Generates F' using DIIS and stores it in FockMatrix.

    Eigen::MatrixXd FockOrtho = SOrtho.transpose() * FockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > EigensystemFockOrtho(FockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    CoeffMatrix = SOrtho * EigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.
    OrbitalEV = EigensystemFockOrtho.eigenvalues();

	/* Density matrix: C(occ) * C(occ)^T */
    if(Input.Options[1]) // Means use MOM
    {
        if(!Bias.empty()) // Means the first SCP loop when there is a bias. Use MOM for this loop.
        {
            MaximumOverlapMethod(DensityMatrix, CoeffMatrix, CoeffMatrixPrev, Input.OverlapMatrix, Input.NumOcc, Input.NumAO, OccupiedOrbitals, VirtualOrbitals); // MoM 
            CoeffMatrixPrev = CoeffMatrix; // Now that we finish the MoM iteration, set CoeffMatrixPrev.
        }
        else // Then remove the bias and lock in the orbitals.
        {
           for (int i = 0; i < DensityMatrix.rows(); i++)
           {
                for (int j = 0; j < DensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.NumOcc; k++)
                    {
                        DensityElement += CoeffMatrix(i, OccupiedOrbitals[k]) * CoeffMatrix(j, OccupiedOrbitals[k]);
                    }
                    DensityMatrix(i, j) = DensityElement;
                }
            }
        }
    }
    else // Means do not use MOM
    {
        for (int i = 0; i < DensityMatrix.rows(); i++)
        {
            for (int j = 0; j < DensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.NumOcc; k++)
                {
                    DensityElement += CoeffMatrix(i, OccupiedOrbitals[k]) * CoeffMatrix(j, OccupiedOrbitals[k]);
                }
                DensityMatrix(i, j) = DensityElement;
            }
        }
    }
    // std::cout << "D\n" << 2 * DensityMatrix << std::endl;
    // std::string tmpstring;
    // std::getline(std::cin, tmpstring);

	/* Now calculate the HF energy. E = sum_ij P_ij * (HCore_ij + F_ij) */
    double Energy = (DensityMatrix.cwiseProduct(HCore + FockMatrix)).sum();
    return Energy;
}

double SCF(std::vector< std::tuple< Eigen::MatrixXd, double, double > > &Bias, int SolnNum, Eigen::MatrixXd &DensityMatrix, InputObj &Input, std::ofstream &Output, Eigen::MatrixXd &SOrtho, Eigen::MatrixXd &HCore, std::vector< double > &AllEnergies, Eigen::MatrixXd &CoeffMatrix, std::vector<int> &OccupiedOrbitals, std::vector<int> &VirtualOrbitals, int &SCFCount, int MaxSCF, Eigen::MatrixXd DMETPotential, Eigen::VectorXd &OrbitalEV)
{
	double SCFTol = 1E-5; // 1E-8; // SCF will terminate when the DIIS error is below this amount. 
    std::cout << std::fixed << std::setprecision(10);

	Output << "Beginning search for Solution " << SolnNum << std::endl;
	Output << "Iteration\tEnergy" << std::endl;
	std::cout << "SCF MetaD: Beginning search for Solution " << SolnNum << std::endl;
	clock_t ClockStart = clock();

    double Energy = 1; // HF energy of SCF iteration.
    double DIISError = 1; // Square sum of DIIS error matrix of the current iteration. Used to test convergence.
    Eigen::MatrixXd DensityMatrixPrev; // Stores density matrix of the previous iteration to test density matrix convergence.
    double EnergyPrev = 1; // Stores energy of previous iteration to test energy convergence.
    double DensityRMS = 1; // Stores squared different between two sequential density matrices.
    unsigned short int Count = 1; // Counts number of iterations.
    bool isUniqueSoln = false; // Will tell us if the solution is unique by checking against all previous energies.
    bool ContinueSCF = true; // Tells us when SCF is converged, based on whatever criterion is selected.
    
    while(!isUniqueSoln)
    {
        std::vector< Eigen::MatrixXd > AllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > AllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd CoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        ContinueSCF = true;
        Count = 1;
        while((ContinueSCF || Count < 5) && !Bias.empty()) // Do 15 times atleast, but skip if this is the first SCF.
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                DensityMatrixPrev = DensityMatrix;
            }
            Energy = SCFIteration(DensityMatrix, Input, HCore, SOrtho, Bias, CoeffMatrix, AllFockMatrices, AllErrorMatrices, CoeffMatrixPrev, OccupiedOrbitals, VirtualOrbitals, DMETPotential, OrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (DensityMatrix - DensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (DensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(AllErrorMatrices[AllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with a biased energy of " << Energy + Input.Integrals["0 0 0 0"];
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl; // I planned to list the energy of each iteration, but there is a ridiculous amount of iterations.
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            /* This is a work-around that I put in. The first guess of the density is a zero matrix and this is not good. Unfortunately, DIIS
               rarely corrects this so I find that it helps to clear the Fock and Error matrices after a few iterations and we have a more reasonable
               guess of the coefficient, and thus density, matrices. Then DIIS converges to a reasonable solution. */
            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 200 == 0) // Shouldn't take this long.
            {
                Count = 0;
                AllFockMatrices.clear();
                AllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        } // Means we have converged with the bias. Now we remove the bias and converge to the minimum

        Count = 1;
        std::vector< std::tuple< Eigen::MatrixXd, double, double > > EmptyBias; // Same type as Bias, but it's empty so it's the same as having no bias.
        AllFockMatrices.clear();
        AllErrorMatrices.clear();
        ContinueSCF = true; // Reset for the next loop to start.

        while(ContinueSCF || Count < 5)
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                DensityMatrixPrev = DensityMatrix;
            }
            Energy = SCFIteration(DensityMatrix, Input, HCore, SOrtho, EmptyBias, CoeffMatrix, AllFockMatrices, AllErrorMatrices, CoeffMatrixPrev, OccupiedOrbitals, VirtualOrbitals, DMETPotential, OrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (DensityMatrix - DensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (DensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = AllErrorMatrices[AllErrorMatrices.size() - 1].squaredNorm();
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with an energy of " << Energy + Input.Integrals["0 0 0 0"];//  << " and DIIS error of " << DIISError << std::endl;
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl;
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 200 == 0)
            {
                Count = 0;
                AllFockMatrices.clear();
                AllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        }

        isUniqueSoln = true;
        short int WhichSoln = -1; // If we found a solution we already visited, this will mark which of the previous solutions we are at.
        // if(Energy + Input.Integrals["0 0 0 0"] > 0) // Hopefully we won't be dissociating.
        // {
        //     isUniqueSoln = false;
        // }
        // else
        // {
            for(int i = 0; i < AllEnergies.size(); i++) // Compare energy with previous solutions.
            {
                if(fabs(Energy + Input.Integrals["0 0 0 0"] - AllEnergies[i]) < 1E-5) // Checks to see if new energy is equal to any previous energy.
                {
                    isUniqueSoln = false; // If it matches at one, set this flag to false so the SCF procedure can repeat for this solution.
                    WhichSoln = i;
                    break;
                }
            }
            if(isUniqueSoln) // If it still looks good
            {
                for(int i = 0; i < Bias.size(); i++)
                {
                    if((DensityMatrix - std::get<0>(Bias[i])).squaredNorm() < 1E-3) // Means same density matrix as found before
                    {
                        isUniqueSoln = false;
                        WhichSoln = i;
                        break;
                    }
                }
            }
        // }

        if(!isUniqueSoln) // If the flag is still false, we modify the bias and hope that this gives a better result.
        {
            std::cout << "SCF MetaD: Solution is not unique. Retrying solution " << SolnNum << "." << std::endl;
            ModifyBias(Bias, WhichSoln); // Changes bias, usually means increase value of parameters.

            /* We should also change the density matrix to converge to different solution, but it is not
               obvious how we should do that. We could rotate two orbitals, but this may not be enough to
               find a different solution. We could randomize the density matrix, but then we get 
               unphysical results. */
            if(Input.DensityOption == 0) NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
            if(Input.DensityOption == 1) GenerateRandomDensity(DensityMatrix);
            if(Input.DensityOption == 2) DensityMatrix = Eigen::MatrixXd::Random(DensityMatrix.rows(), DensityMatrix.cols());
            if(Input.DensityOption == 3) GenerateRandomDensityTS(DensityMatrix);
        }
    }

    AllEnergies.push_back(Energy + Input.Integrals["0 0 0 0"]);

	std::cout << "SCF MetaD: Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
	std::cout << "SCF MetaD: This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;
	Output << "Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
    Output << "and orbitals:" << std::endl;
    Output << "Basis\tMolecular Orbitals" << std::endl;
    for(int mu = 0; mu < CoeffMatrix.rows(); mu++)
    {
        Output << mu + 1;
        for(int i = 0; i < OccupiedOrbitals.size(); i++) // Loop through each molecular orbital
        {
            Output << "\t" << CoeffMatrix(mu, OccupiedOrbitals[i]); // Select the columns corresponding to the occupied orbitals.
        }
        Output << "\n";
    }
    
	Output << "This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;

    return Energy + Input.Integrals["0 0 0 0"];
}

/******* OVERLOADED FUNCTION FOR UNRESTRICTED HARTREE FOCK ********/
double SCFIteration(Eigen::MatrixXd &aDensityMatrix, Eigen::MatrixXd &bDensityMatrix, InputObj &Input, Eigen::MatrixXd &HCore, Eigen::MatrixXd &SOrtho, 
                    std::vector< std::tuple< Eigen::MatrixXd, double, double > > &aBias, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &bBias, Eigen::MatrixXd &aCoeffMatrix, Eigen::MatrixXd &bCoeffMatrix,
                    std::vector< Eigen::MatrixXd > &aAllFockMatrices, std::vector< Eigen::MatrixXd > &bAllFockMatrices, std::vector< Eigen::MatrixXd > &aAllErrorMatrices, std::vector< Eigen::MatrixXd > &bAllErrorMatrices, 
                    Eigen::MatrixXd &aCoeffMatrixPrev, Eigen::MatrixXd &bCoeffMatrixPrev, 
                    std::vector<int> &aOccupiedOrbitals, std::vector<int> &bOccupiedOrbitals, std::vector<int> &aVirtualOrbitals, std::vector<int> &bVirtualOrbitals, 
                    Eigen::MatrixXd DMETPotential, Eigen::VectorXd &aOrbitalEV, Eigen::VectorXd &bOrbitalEV)
{
    Eigen::MatrixXd aFockMatrix(aDensityMatrix.rows(), aDensityMatrix.cols()); // This will hold the FockMatrix.
    Eigen::MatrixXd bFockMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    // Eigen::MatrixXd aCoulombMatrix(aDensityMatrix.rows(), aDensityMatrix.cols());
    // Eigen::MatrixXd bCoulombMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    // Eigen::MatrixXd aExchangeMatrix(aDensityMatrix.rows(), aDensityMatrix.cols());
    // Eigen::MatrixXd bExchangeMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    BuildFockMatrix(aFockMatrix, aDensityMatrix, bDensityMatrix, Input.Integrals, aBias, Input.NumElectrons); // Calculates and stores fock matrix. Includes bias.
    BuildFockMatrix(bFockMatrix, bDensityMatrix, aDensityMatrix, Input.Integrals, bBias, Input.NumElectrons);
    aFockMatrix += DMETPotential; // If before DIIS, results in improper Fock matrix. If after DIIS, gradient is wrong.
    bFockMatrix += DMETPotential; 
    aAllFockMatrices.push_back(aFockMatrix); // Store this iteration's Fock matrix for the DIIS procedure.
    bAllFockMatrices.push_back(bFockMatrix);

    Eigen::MatrixXd aErrorMatrix = aFockMatrix * aDensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * aDensityMatrix * aFockMatrix; // DIIS error matrix of the current iteration: FPS - SPF
    Eigen::MatrixXd bErrorMatrix = bFockMatrix * bDensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * bDensityMatrix * bFockMatrix;
    aAllErrorMatrices.push_back(aErrorMatrix); // Save error matrix for DIIS.
    bAllErrorMatrices.push_back(bErrorMatrix);
    DIIS(aFockMatrix, aAllFockMatrices, aAllErrorMatrices); // Generates F' using DIIS and stores it in FockMatrix.
    DIIS(bFockMatrix, bAllFockMatrices, bAllErrorMatrices);

    if (aAllFockMatrices.size() >= 5) // We only want about 5 Fock matrices for DIIS, so we'll throw away the earlier ones.
	{
		aAllFockMatrices.erase(aAllFockMatrices.begin(), aAllFockMatrices.begin() + 1);
		aAllErrorMatrices.erase(aAllErrorMatrices.begin(), aAllErrorMatrices.begin() + 1);
        bAllFockMatrices.erase(bAllFockMatrices.begin(), bAllFockMatrices.begin() + 1);
		bAllErrorMatrices.erase(bAllErrorMatrices.begin(), bAllErrorMatrices.begin() + 1);
	}

    Eigen::MatrixXd aFockOrtho = SOrtho.transpose() * aFockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > aEigensystemFockOrtho(aFockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    aCoeffMatrix = SOrtho * aEigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.
    aOrbitalEV = aEigensystemFockOrtho.eigenvalues();

    Eigen::MatrixXd bFockOrtho = SOrtho.transpose() * bFockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > bEigensystemFockOrtho(bFockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    bCoeffMatrix = SOrtho * bEigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.
    bOrbitalEV = bEigensystemFockOrtho.eigenvalues();

	/* Density matrix: C(occ) * C(occ)^T */
    if(Input.Options[1]) // Means use MOM
    {
        if(!aBias.empty()) // Means the first SCP loop when there is a bias. Use MOM for this loop.
        {
            MaximumOverlapMethod(aDensityMatrix, aCoeffMatrix, aCoeffMatrixPrev, Input.OverlapMatrix, Input.aNumElectrons, Input.NumAO, aOccupiedOrbitals, aVirtualOrbitals); // MoM 
            MaximumOverlapMethod(bDensityMatrix, bCoeffMatrix, bCoeffMatrixPrev, Input.OverlapMatrix, Input.bNumElectrons, Input.NumAO, bOccupiedOrbitals, bVirtualOrbitals);
            aCoeffMatrixPrev = aCoeffMatrix; // Now that we finish the MoM iteration, set CoeffMatrixPrev.
            bCoeffMatrixPrev = bCoeffMatrix;
        }
        else // Then remove the bias and lock in the orbitals.
        {
           for (int i = 0; i < aDensityMatrix.rows(); i++)
           {
                for (int j = 0; j < aDensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.aNumElectrons; k++)
                    {
                        DensityElement += aCoeffMatrix(i, aOccupiedOrbitals[k]) * aCoeffMatrix(j, aOccupiedOrbitals[k]);
                    }
                    aDensityMatrix(i, j) = DensityElement;
                }
            }
            for (int i = 0; i < bDensityMatrix.rows(); i++)
            {
                for (int j = 0; j < bDensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.bNumElectrons; k++)
                    {
                        DensityElement += bCoeffMatrix(i, bOccupiedOrbitals[k]) * bCoeffMatrix(j, bOccupiedOrbitals[k]);
                    }
                    bDensityMatrix(i, j) = DensityElement;
                }
            }
        }
    }
    else // Means do not use MOM
    {
        for (int i = 0; i < aDensityMatrix.rows(); i++)
        {
            for (int j = 0; j < aDensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.aNumElectrons; k++)
                {
                    DensityElement += aCoeffMatrix(i, aOccupiedOrbitals[k]) * aCoeffMatrix(j, aOccupiedOrbitals[k]);
                }
                aDensityMatrix(i, j) = DensityElement;
            }
        }
        for (int i = 0; i < bDensityMatrix.rows(); i++)
        {
            for (int j = 0; j < bDensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.bNumElectrons; k++)
                {
                    DensityElement += bCoeffMatrix(i, bOccupiedOrbitals[k]) * bCoeffMatrix(j, bOccupiedOrbitals[k]);
                }
                bDensityMatrix(i, j) = DensityElement;
            }
        }
    }
    // std::cout << "D\n" << 2 * DensityMatrix << std::endl;
    // std::string tmpstring;
    // std::getline(std::cin, tmpstring);

	/* Now calculate the HF energy. E = sum_ij P_ij * (HCore_ij + F_ij) */
    double Energy = 0.5 * (aDensityMatrix.cwiseProduct(HCore + aFockMatrix)).sum()
                  + 0.5 * (bDensityMatrix.cwiseProduct(HCore + bFockMatrix)).sum(); // I don't know what this should be.
    return Energy;
}

double SCF(std::vector< std::tuple< Eigen::MatrixXd, double, double > > &aBias, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &bBias, int SolnNum, Eigen::MatrixXd &aDensityMatrix, Eigen::MatrixXd &bDensityMatrix, InputObj &Input, std::ofstream &Output, Eigen::MatrixXd &SOrtho, Eigen::MatrixXd &HCore, std::vector< double > &AllEnergies, 
           Eigen::MatrixXd &aCoeffMatrix, Eigen::MatrixXd &bCoeffMatrix, std::vector<int> &aOccupiedOrbitals, std::vector<int> &bOccupiedOrbitals, std::vector<int> &aVirtualOrbitals, std::vector<int> &bVirtualOrbitals, 
           int &SCFCount, int MaxSCF, Eigen::MatrixXd DMETPotential, Eigen::VectorXd &aOrbitalEV, Eigen::VectorXd &bOrbitalEV)
{
	double SCFTol = 1E-3; // 1E-8; // SCF will terminate when the DIIS error is below this amount. 
    std::cout << std::fixed << std::setprecision(10);

	Output << "Beginning search for Solution " << SolnNum << std::endl;
	Output << "Iteration\tEnergy" << std::endl;
	std::cout << "SCF MetaD: Beginning search for Solution " << SolnNum << std::endl;
	clock_t ClockStart = clock();

    double Energy = 1; // HF energy of SCF iteration.
    double DIISError = 1; // Square sum of DIIS error matrix of the current iteration. Used to test convergence.
    Eigen::MatrixXd aDensityMatrixPrev; // Stores density matrix of the previous iteration to test density matrix convergence.
    Eigen::MatrixXd bDensityMatrixPrev;
    double EnergyPrev = 1; // Stores energy of previous iteration to test energy convergence.
    double DensityRMS = 1; // Stores squared different between two sequential density matrices.
    unsigned short int Count = 1; // Counts number of iterations.
    bool isUniqueSoln = false; // Will tell us if the solution is unique by checking against all previous energies.
    bool ContinueSCF = true; // Tells us when SCF is converged, based on whatever criterion is selected.
    
    while(!isUniqueSoln)
    {
        std::vector< Eigen::MatrixXd > aAllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > aAllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd aCoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        std::vector< Eigen::MatrixXd > bAllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > bAllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd bCoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        ContinueSCF = true;
        Count = 1;
        while((ContinueSCF || Count < 5) && !aBias.empty()) // Do 15 times atleast, but skip if this is the first SCF.
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                aDensityMatrixPrev = aDensityMatrix;
                bDensityMatrixPrev = bDensityMatrix;
            }
            Energy = SCFIteration(aDensityMatrix, bDensityMatrix, Input, HCore, SOrtho, aBias, bBias, aCoeffMatrix, bCoeffMatrix, aAllFockMatrices, bAllFockMatrices, aAllErrorMatrices, bAllErrorMatrices, aCoeffMatrixPrev, bCoeffMatrixPrev, aOccupiedOrbitals, bOccupiedOrbitals, aVirtualOrbitals, bVirtualOrbitals, DMETPotential, aOrbitalEV, bOrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (aDensityMatrix - aDensityMatrixPrev).squaredNorm() + (bDensityMatrix - bDensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (aDensityMatrix.squaredNorm() + bDensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(aAllErrorMatrices[aAllErrorMatrices.size() - 1]) + CalcMatrixRMS(bAllErrorMatrices[bAllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with a biased energy of " << Energy + Input.Integrals["0 0 0 0"];
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl; // I planned to list the energy of each iteration, but there is a ridiculous amount of iterations.
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            /* This is a work-around that I put in. The first guess of the density is a zero matrix and this is not good. Unfortunately, DIIS
               rarely corrects this so I find that it helps to clear the Fock and Error matrices after a few iterations and we have a more reasonable
               guess of the coefficient, and thus density, matrices. Then DIIS converges to a reasonable solution. */
            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 500 == 0) // Shouldn't take this long.
            {
                Count = 0;
                aAllFockMatrices.clear();
                aAllErrorMatrices.clear();
                bAllFockMatrices.clear();
                bAllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        } // Means we have converged with the bias. Now we remove the bias and converge to the minimum

        Count = 1;
        std::vector< std::tuple< Eigen::MatrixXd, double, double > > EmptyBias; // Same type as Bias, but it's empty so it's the same as having no bias.
        aAllFockMatrices.clear();
        aAllErrorMatrices.clear();
        bAllFockMatrices.clear();
        bAllErrorMatrices.clear();
        ContinueSCF = true; // Reset for the next loop to start.

        while(ContinueSCF || Count < 5)
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                aDensityMatrixPrev = aDensityMatrix;
                bDensityMatrixPrev = bDensityMatrix;
            }
            Energy = SCFIteration(aDensityMatrix, bDensityMatrix, Input, HCore, SOrtho, EmptyBias, EmptyBias, aCoeffMatrix, bCoeffMatrix, aAllFockMatrices, bAllFockMatrices, aAllErrorMatrices, bAllErrorMatrices, aCoeffMatrixPrev, bCoeffMatrixPrev, aOccupiedOrbitals, bOccupiedOrbitals, aVirtualOrbitals, bVirtualOrbitals, DMETPotential, aOrbitalEV, bOrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (aDensityMatrix - aDensityMatrixPrev).squaredNorm() + (bDensityMatrix - bDensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (aDensityMatrix.squaredNorm() + bDensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(aAllErrorMatrices[aAllErrorMatrices.size() - 1]) + CalcMatrixRMS(bAllErrorMatrices[bAllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with an energy of " << Energy + Input.Integrals["0 0 0 0"];//  << " and DIIS error of " << DIISError << std::endl;
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl;
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 500 == 0)
            {
                Count = 0;
                aAllFockMatrices.clear();
                aAllErrorMatrices.clear();
                bAllFockMatrices.clear();
                bAllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        }

        isUniqueSoln = true;
        short int WhichSoln = -1; // If we found a solution we already visited, this will mark which of the previous solutions we are at.
        // if(Energy + Input.Integrals["0 0 0 0"] > 0) // Hopefully we won't be dissociating.
        // {
        //     isUniqueSoln = false;
        // }
        // else
        // {
            for(int i = 0; i < AllEnergies.size(); i++) // Compare energy with previous solutions.
            {
                if(fabs(Energy + Input.Integrals["0 0 0 0"] - AllEnergies[i]) < 1E-5) // Checks to see if new energy is equal to any previous energy.
                {
                    isUniqueSoln = false; // If it matches at one, set this flag to false so the SCF procedure can repeat for this solution.
                    WhichSoln = i;
                    break;
                }
            }
            if(isUniqueSoln) // If it still looks good
            {
                for(int i = 0; i < aBias.size(); i++)
                {
                    if((aDensityMatrix - std::get<0>(aBias[i])).squaredNorm() < 1E-3 && (bDensityMatrix - std::get<0>(bBias[i])).squaredNorm() < 1E-3) // Means same density matrix as found before
                    {
                        isUniqueSoln = false;
                        WhichSoln = i;
                        break;
                    }
                }
            }
        // }

        if(!isUniqueSoln) // If the flag is still false, we modify the bias and hope that this gives a better result.
        {
            std::cout << "SCF MetaD: Solution is not unique. Retrying solution " << SolnNum << "." << std::endl;
            ModifyBias(aBias, WhichSoln); // Changes bias, usually means increase value of parameters.
            ModifyBias(bBias, WhichSoln);

            /* We should also change the density matrix to converge to different solution, but it is not
               obvious how we should do that. We could rotate two orbitals, but this may not be enough to
               find a different solution. We could randomize the density matrix, but then we get 
               unphysical results. */
            if(Input.DensityOption == 0) 
            {
                NewDensityMatrix(aDensityMatrix, aCoeffMatrix, aOccupiedOrbitals, aVirtualOrbitals);
                NewDensityMatrix(bDensityMatrix, bCoeffMatrix, bOccupiedOrbitals, bVirtualOrbitals);
            }
            if(Input.DensityOption == 1) 
            {
                GenerateRandomDensity(aDensityMatrix);
                GenerateRandomDensity(bDensityMatrix);
            }
            if(Input.DensityOption == 2) 
            {
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
            }
            if(Input.DensityOption == 3) 
            {
                GenerateRandomDensityTS(aDensityMatrix);
                GenerateRandomDensityTS(bDensityMatrix);
            }
        }
    }

    AllEnergies.push_back(Energy + Input.Integrals["0 0 0 0"]);

	std::cout << "SCF MetaD: Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
	std::cout << "SCF MetaD: This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;
	Output << "Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
    // Output << "and orbitals:" << std::endl;
    // Output << "Basis\tMolecular Orbitals" << std::endl;
    // for(int mu = 0; mu < CoeffMatrix.rows(); mu++)
    // {
    //     Output << mu + 1;
    //     for(int i = 0; i < OccupiedOrbitals.size(); i++) // Loop through each molecular orbital
    //     {
    //         Output << "\t" << CoeffMatrix(mu, OccupiedOrbitals[i]); // Select the columns corresponding to the occupied orbitals.
    //     }
    //     Output << "\n";
    // }
    
	Output << "This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;

    return Energy + Input.Integrals["0 0 0 0"];
}

/******* OVERLOADED FUNCTION FOR UNRESTRICTED HARTREE FOCK WITH DIFFERENT SPIN INTEGRALS ********/
double SCFIteration(Eigen::MatrixXd &aDensityMatrix, Eigen::MatrixXd &bDensityMatrix, InputObj &Input, Eigen::MatrixXd &aHCore, Eigen::MatrixXd &bHCore, Eigen::MatrixXd &SOrtho, 
                    std::vector< std::tuple< Eigen::MatrixXd, double, double > > &aBias, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &bBias, Eigen::MatrixXd &aCoeffMatrix, Eigen::MatrixXd &bCoeffMatrix,
                    std::vector< Eigen::MatrixXd > &aAllFockMatrices, std::vector< Eigen::MatrixXd > &bAllFockMatrices, std::vector< Eigen::MatrixXd > &aAllErrorMatrices, std::vector< Eigen::MatrixXd > &bAllErrorMatrices, 
                    Eigen::MatrixXd &aCoeffMatrixPrev, Eigen::MatrixXd &bCoeffMatrixPrev, 
                    std::vector<int> &aOccupiedOrbitals, std::vector<int> &bOccupiedOrbitals, std::vector<int> &aVirtualOrbitals, std::vector<int> &bVirtualOrbitals, 
                    Eigen::MatrixXd DMETPotential, Eigen::VectorXd &aOrbitalEV, Eigen::VectorXd &bOrbitalEV)
{
    Eigen::MatrixXd aFockMatrix(aDensityMatrix.rows(), aDensityMatrix.cols()); // This will hold the FockMatrix.
    Eigen::MatrixXd bFockMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    // Eigen::MatrixXd aCoulombMatrix(aDensityMatrix.rows(), aDensityMatrix.cols());
    // Eigen::MatrixXd bCoulombMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    // Eigen::MatrixXd aExchangeMatrix(aDensityMatrix.rows(), aDensityMatrix.cols());
    // Eigen::MatrixXd bExchangeMatrix(bDensityMatrix.rows(), bDensityMatrix.cols());
    BuildFockMatrix(aFockMatrix, aDensityMatrix, bDensityMatrix, Input.aaIntegrals, Input.abIntegrals, aBias, Input.NumElectrons); // Calculates and stores fock matrix. Includes bias.
    BuildFockMatrix(bFockMatrix, bDensityMatrix, aDensityMatrix, Input.aaIntegrals, Input.baIntegrals, bBias, Input.NumElectrons);
    aFockMatrix += DMETPotential; // If before DIIS, results in improper Fock matrix. If after DIIS, gradient is wrong.
    bFockMatrix += DMETPotential; 
    aAllFockMatrices.push_back(aFockMatrix); // Store this iteration's Fock matrix for the DIIS procedure.
    bAllFockMatrices.push_back(bFockMatrix);

    Eigen::MatrixXd aErrorMatrix = aFockMatrix * aDensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * aDensityMatrix * aFockMatrix; // DIIS error matrix of the current iteration: FPS - SPF
    Eigen::MatrixXd bErrorMatrix = bFockMatrix * bDensityMatrix * Input.OverlapMatrix - Input.OverlapMatrix * bDensityMatrix * bFockMatrix;
    aAllErrorMatrices.push_back(aErrorMatrix); // Save error matrix for DIIS.
    bAllErrorMatrices.push_back(bErrorMatrix);
    DIIS(aFockMatrix, aAllFockMatrices, aAllErrorMatrices); // Generates F' using DIIS and stores it in FockMatrix.
    DIIS(bFockMatrix, bAllFockMatrices, bAllErrorMatrices);

    if (aAllFockMatrices.size() >= 5) // We only want about 5 Fock matrices for DIIS, so we'll throw away the earlier ones.
	{
		aAllFockMatrices.erase(aAllFockMatrices.begin(), aAllFockMatrices.begin() + 1);
		aAllErrorMatrices.erase(aAllErrorMatrices.begin(), aAllErrorMatrices.begin() + 1);
        bAllFockMatrices.erase(bAllFockMatrices.begin(), bAllFockMatrices.begin() + 1);
		bAllErrorMatrices.erase(bAllErrorMatrices.begin(), bAllErrorMatrices.begin() + 1);
	}

    Eigen::MatrixXd aFockOrtho = SOrtho.transpose() * aFockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > aEigensystemFockOrtho(aFockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    aCoeffMatrix = SOrtho * aEigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.
    aOrbitalEV = aEigensystemFockOrtho.eigenvalues();

    Eigen::MatrixXd bFockOrtho = SOrtho.transpose() * bFockMatrix * SOrtho; // Fock matrix in orthonormal basis.
    Eigen::SelfAdjointEigenSolver< Eigen::MatrixXd > bEigensystemFockOrtho(bFockOrtho); // Eigenvectors and eigenvalues ordered from lowest to highest eigenvalues
    bCoeffMatrix = SOrtho * bEigensystemFockOrtho.eigenvectors(); // Multiply the matrix of coefficients by S^-1/2 to get coefficients for nonorthonormal basis.
    bOrbitalEV = bEigensystemFockOrtho.eigenvalues();

	/* Density matrix: C(occ) * C(occ)^T */
    if(Input.Options[1]) // Means use MOM
    {
        if(!aBias.empty()) // Means the first SCP loop when there is a bias. Use MOM for this loop.
        {
            MaximumOverlapMethod(aDensityMatrix, aCoeffMatrix, aCoeffMatrixPrev, Input.OverlapMatrix, Input.aNumElectrons, Input.NumAO, aOccupiedOrbitals, aVirtualOrbitals); // MoM 
            MaximumOverlapMethod(bDensityMatrix, bCoeffMatrix, bCoeffMatrixPrev, Input.OverlapMatrix, Input.bNumElectrons, Input.NumAO, bOccupiedOrbitals, bVirtualOrbitals);
            aCoeffMatrixPrev = aCoeffMatrix; // Now that we finish the MoM iteration, set CoeffMatrixPrev.
            bCoeffMatrixPrev = bCoeffMatrix;
        }
        else // Then remove the bias and lock in the orbitals.
        {
           for (int i = 0; i < aDensityMatrix.rows(); i++)
           {
                for (int j = 0; j < aDensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.aNumElectrons; k++)
                    {
                        DensityElement += aCoeffMatrix(i, aOccupiedOrbitals[k]) * aCoeffMatrix(j, aOccupiedOrbitals[k]);
                    }
                    aDensityMatrix(i, j) = DensityElement;
                }
            }
            for (int i = 0; i < bDensityMatrix.rows(); i++)
            {
                for (int j = 0; j < bDensityMatrix.cols(); j++)
                {
                    double DensityElement = 0;
                    for (int k = 0; k < Input.bNumElectrons; k++)
                    {
                        DensityElement += bCoeffMatrix(i, bOccupiedOrbitals[k]) * bCoeffMatrix(j, bOccupiedOrbitals[k]);
                    }
                    bDensityMatrix(i, j) = DensityElement;
                }
            }
        }
    }
    else // Means do not use MOM
    {
        for (int i = 0; i < aDensityMatrix.rows(); i++)
        {
            for (int j = 0; j < aDensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.aNumElectrons; k++)
                {
                    DensityElement += aCoeffMatrix(i, aOccupiedOrbitals[k]) * aCoeffMatrix(j, aOccupiedOrbitals[k]);
                }
                aDensityMatrix(i, j) = DensityElement;
            }
        }
        for (int i = 0; i < bDensityMatrix.rows(); i++)
        {
            for (int j = 0; j < bDensityMatrix.cols(); j++)
            {
                double DensityElement = 0;
                for (int k = 0; k < Input.bNumElectrons; k++)
                {
                    DensityElement += bCoeffMatrix(i, bOccupiedOrbitals[k]) * bCoeffMatrix(j, bOccupiedOrbitals[k]);
                }
                bDensityMatrix(i, j) = DensityElement;
            }
        }
    }
    // std::cout << "D\n" << 2 * DensityMatrix << std::endl;
    // std::string tmpstring;
    // std::getline(std::cin, tmpstring);

	/* Now calculate the HF energy. E = sum_ij P_ij * (HCore_ij + F_ij) */
    double Energy = 0.5 * (aDensityMatrix.cwiseProduct(aHCore + aFockMatrix)).sum()
                  + 0.5 * (bDensityMatrix.cwiseProduct(bHCore + bFockMatrix)).sum(); // I don't know what this should be.
    return Energy;
}

double SCF(std::vector< std::tuple< Eigen::MatrixXd, double, double > > &aBias, std::vector< std::tuple< Eigen::MatrixXd, double, double > > &bBias, int SolnNum, Eigen::MatrixXd &aDensityMatrix, Eigen::MatrixXd &bDensityMatrix, InputObj &Input, std::ofstream &Output, Eigen::MatrixXd &SOrtho, Eigen::MatrixXd &aHCore, Eigen::MatrixXd &bHCore, std::vector< double > &AllEnergies, 
           Eigen::MatrixXd &aCoeffMatrix, Eigen::MatrixXd &bCoeffMatrix, std::vector<int> &aOccupiedOrbitals, std::vector<int> &bOccupiedOrbitals, std::vector<int> &aVirtualOrbitals, std::vector<int> &bVirtualOrbitals, 
           int &SCFCount, int MaxSCF, Eigen::MatrixXd DMETPotential, Eigen::VectorXd &aOrbitalEV, Eigen::VectorXd &bOrbitalEV)
{
	double SCFTol = 1E-5; // 1E-8; // SCF will terminate when the DIIS error is below this amount. 
    std::cout << std::fixed << std::setprecision(10);

	Output << "Beginning search for Solution " << SolnNum << std::endl;
	Output << "Iteration\tEnergy" << std::endl;
	std::cout << "SCF MetaD: Beginning search for Solution " << SolnNum << std::endl;
	clock_t ClockStart = clock();

    double Energy = 1; // HF energy of SCF iteration.
    double DIISError = 1; // Square sum of DIIS error matrix of the current iteration. Used to test convergence.
    Eigen::MatrixXd aDensityMatrixPrev; // Stores density matrix of the previous iteration to test density matrix convergence.
    Eigen::MatrixXd bDensityMatrixPrev;
    double EnergyPrev = 1; // Stores energy of previous iteration to test energy convergence.
    double DensityRMS = 1; // Stores squared different between two sequential density matrices.
    unsigned short int Count = 1; // Counts number of iterations.
    bool isUniqueSoln = false; // Will tell us if the solution is unique by checking against all previous energies.
    bool ContinueSCF = true; // Tells us when SCF is converged, based on whatever criterion is selected.
    
    while(!isUniqueSoln)
    {
        std::vector< Eigen::MatrixXd > aAllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > aAllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd aCoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        std::vector< Eigen::MatrixXd > bAllFockMatrices; // Holds previous fock matrices for DIIS procedure.
        std::vector< Eigen::MatrixXd > bAllErrorMatrices; // Error matrices for DIIS
        Eigen::MatrixXd bCoeffMatrixPrev = Eigen::MatrixXd::Identity(Input.NumAO, Input.NumAO); // Two sequential coefficient matrices are stored for MOM.
        ContinueSCF = true;
        Count = 1;
        while((ContinueSCF || Count < 5) && !aBias.empty()) // Do 15 times atleast, but skip if this is the first SCF.
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                aDensityMatrixPrev = aDensityMatrix;
                bDensityMatrixPrev = bDensityMatrix;
            }
            Energy = SCFIteration(aDensityMatrix, bDensityMatrix, Input, aHCore, bHCore, SOrtho, aBias, bBias, aCoeffMatrix, bCoeffMatrix, aAllFockMatrices, bAllFockMatrices, aAllErrorMatrices, bAllErrorMatrices, aCoeffMatrixPrev, bCoeffMatrixPrev, aOccupiedOrbitals, bOccupiedOrbitals, aVirtualOrbitals, bVirtualOrbitals, DMETPotential, aOrbitalEV, bOrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (aDensityMatrix - aDensityMatrixPrev).squaredNorm() + (bDensityMatrix - bDensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (aDensityMatrix.squaredNorm() + bDensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(aAllErrorMatrices[aAllErrorMatrices.size() - 1]) + CalcMatrixRMS(bAllErrorMatrices[bAllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with a biased energy of " << Energy + Input.Integrals["0 0 0 0"];
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl; // I planned to list the energy of each iteration, but there is a ridiculous amount of iterations.
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            /* This is a work-around that I put in. The first guess of the density is a zero matrix and this is not good. Unfortunately, DIIS
               rarely corrects this so I find that it helps to clear the Fock and Error matrices after a few iterations and we have a more reasonable
               guess of the coefficient, and thus density, matrices. Then DIIS converges to a reasonable solution. */
            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 500 == 0) // Shouldn't take this long.
            {
                Count = 0;
                aAllFockMatrices.clear();
                aAllErrorMatrices.clear();
                bAllFockMatrices.clear();
                bAllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        } // Means we have converged with the bias. Now we remove the bias and converge to the minimum

        Count = 1;
        std::vector< std::tuple< Eigen::MatrixXd, double, double > > EmptyBias; // Same type as Bias, but it's empty so it's the same as having no bias.
        aAllFockMatrices.clear();
        aAllErrorMatrices.clear();
        bAllFockMatrices.clear();
        bAllErrorMatrices.clear();
        ContinueSCF = true; // Reset for the next loop to start.

        while(ContinueSCF || Count < 5)
        {
            std::cout << "SCF MetaD: Iteration " << Count << "...";
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
                EnergyPrev = Energy;
                aDensityMatrixPrev = aDensityMatrix;
                bDensityMatrixPrev = bDensityMatrix;
            }
            Energy = SCFIteration(aDensityMatrix, bDensityMatrix, Input, aHCore, bHCore, SOrtho, EmptyBias, EmptyBias, aCoeffMatrix, bCoeffMatrix, aAllFockMatrices, bAllFockMatrices, aAllErrorMatrices, bAllErrorMatrices, aCoeffMatrixPrev, bCoeffMatrixPrev, aOccupiedOrbitals, bOccupiedOrbitals, aVirtualOrbitals, bVirtualOrbitals, DMETPotential, aOrbitalEV, bOrbitalEV);
            if(!Input.Options[0]) // Don't use DIIS. Check matrix RMS instead.
            {
               DensityRMS = (aDensityMatrix - aDensityMatrixPrev).squaredNorm() + (bDensityMatrix - bDensityMatrixPrev).squaredNorm();
               if(fabs(DensityRMS) < SCFTol * SCFTol * (aDensityMatrix.squaredNorm() + bDensityMatrix.squaredNorm() + 1) && fabs(Energy - EnergyPrev) < SCFTol * SCFTol * (fabs(Energy) + 1))
               {
                   ContinueSCF = false;
               }
               else
               {
                   ContinueSCF = true;
               }
            }
            else // Use DIIS, check DIIS error instead.
            {
                DIISError = CalcMatrixRMS(aAllErrorMatrices[aAllErrorMatrices.size() - 1]) + CalcMatrixRMS(bAllErrorMatrices[bAllErrorMatrices.size() - 1]);
                if(fabs(DIISError) < SCFTol * SCFTol)
                {
                    ContinueSCF = false;
                }
                else
                {
                    ContinueSCF = true;
                }
            }
            std::cout << " complete with an energy of " << Energy + Input.Integrals["0 0 0 0"];//  << " and DIIS error of " << DIISError << std::endl;
            if(Input.Options[0])
            {
                std::cout << " and DIIS error of " << DIISError << std::endl;
            }
            else
            {
                std::cout << " and Density RMS of " << DensityRMS << std::endl;
            }
            Output << Count << "\t" << Energy + Input.Integrals["0 0 0 0"] << std::endl;
            Count++;
            SCFCount++;
            if(SCFCount >= MaxSCF && MaxSCF != -1) return 0;

            // if(Count == 5)
            // {
            //     AllFockMatrices.clear();
            //     AllErrorMatrices.clear();
            // }

            if(Count % 500 == 0)
            {
                Count = 0;
                aAllFockMatrices.clear();
                aAllErrorMatrices.clear();
                bAllFockMatrices.clear();
                bAllErrorMatrices.clear();
                // NewDensityMatrix(DensityMatrix, CoeffMatrix, OccupiedOrbitals, VirtualOrbitals);
                // GenerateRandomDensity(DensityMatrix);
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
                // GenerateRandomDensityTS(DensityMatrix);
            }
        }

        isUniqueSoln = true;
        short int WhichSoln = -1; // If we found a solution we already visited, this will mark which of the previous solutions we are at.
        // if(Energy + Input.Integrals["0 0 0 0"] > 0) // Hopefully we won't be dissociating.
        // {
        //     isUniqueSoln = false;
        // }
        // else
        // {
            for(int i = 0; i < AllEnergies.size(); i++) // Compare energy with previous solutions.
            {
                if(fabs(Energy + Input.Integrals["0 0 0 0"] - AllEnergies[i]) < 1E-5) // Checks to see if new energy is equal to any previous energy.
                {
                    isUniqueSoln = false; // If it matches at one, set this flag to false so the SCF procedure can repeat for this solution.
                    WhichSoln = i;
                    break;
                }
            }
            if(isUniqueSoln) // If it still looks good
            {
                for(int i = 0; i < aBias.size(); i++)
                {
                    if((aDensityMatrix - std::get<0>(aBias[i])).squaredNorm() < 1E-3 && (bDensityMatrix - std::get<0>(bBias[i])).squaredNorm() < 1E-3) // Means same density matrix as found before
                    {
                        isUniqueSoln = false;
                        WhichSoln = i;
                        break;
                    }
                }
            }
        // }

        if(!isUniqueSoln) // If the flag is still false, we modify the bias and hope that this gives a better result.
        {
            std::cout << "SCF MetaD: Solution is not unique. Retrying solution " << SolnNum << "." << std::endl;
            ModifyBias(aBias, WhichSoln); // Changes bias, usually means increase value of parameters.
            ModifyBias(bBias, WhichSoln);

            /* We should also change the density matrix to converge to different solution, but it is not
               obvious how we should do that. We could rotate two orbitals, but this may not be enough to
               find a different solution. We could randomize the density matrix, but then we get 
               unphysical results. */
            if(Input.DensityOption == 0) 
            {
                NewDensityMatrix(aDensityMatrix, aCoeffMatrix, aOccupiedOrbitals, aVirtualOrbitals);
                NewDensityMatrix(bDensityMatrix, bCoeffMatrix, bOccupiedOrbitals, bVirtualOrbitals);
            }
            if(Input.DensityOption == 1) 
            {
                GenerateRandomDensity(aDensityMatrix);
                GenerateRandomDensity(bDensityMatrix);
            }
            if(Input.DensityOption == 2) 
            {
                aDensityMatrix = Eigen::MatrixXd::Random(aDensityMatrix.rows(), aDensityMatrix.cols());
                bDensityMatrix = Eigen::MatrixXd::Random(bDensityMatrix.rows(), bDensityMatrix.cols());
            }
            if(Input.DensityOption == 3) 
            {
                GenerateRandomDensityTS(aDensityMatrix);
                GenerateRandomDensityTS(bDensityMatrix);
            }
        }
    }

    AllEnergies.push_back(Energy + Input.Integrals["0 0 0 0"]);

	std::cout << "SCF MetaD: Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
	std::cout << "SCF MetaD: This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;
	Output << "Solution " << SolnNum << " has converged with energy " << Energy + Input.Integrals["0 0 0 0"] << std::endl;
    // Output << "and orbitals:" << std::endl;
    // Output << "Basis\tMolecular Orbitals" << std::endl;
    // for(int mu = 0; mu < CoeffMatrix.rows(); mu++)
    // {
    //     Output << mu + 1;
    //     for(int i = 0; i < OccupiedOrbitals.size(); i++) // Loop through each molecular orbital
    //     {
    //         Output << "\t" << CoeffMatrix(mu, OccupiedOrbitals[i]); // Select the columns corresponding to the occupied orbitals.
    //     }
    //     Output << "\n";
    // }
    
	Output << "This solution took " << (clock() - ClockStart) / CLOCKS_PER_SEC << " seconds." << std::endl;

    return Energy + Input.Integrals["0 0 0 0"];
}

double CalcSCFImpurityEnergy(Eigen::MatrixXd aDensityMatrix, Eigen::MatrixXd bDensityMatrix, std::vector<int> FragPos, double *aOEI, double *bOEI, double *aOEIPlusCore, double *bOEIPlusCore, double *aaTEI, double *abTEI, double *bbTEI)
{
    int N = aDensityMatrix.rows();
    int N4 = N * N * N * N;
    std::vector<double> aaG(N4), abG(N4), baG(N4), bbG(N4);
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
        {
            for (int k = 0; k < N; k++)
            {
                for (int l = 0; l < N; l++)
                {
                    aaG[i * N * N * N + j * N * N + k * N + l] = aDensityMatrix.coeffRef(i, j) * aDensityMatrix.coeffRef(k, l);
                    abG[i * N * N * N + j * N * N + k * N + l] = aDensityMatrix.coeffRef(i, j) * bDensityMatrix.coeffRef(k, l);
                    baG[i * N * N * N + j * N * N + k * N + l] = bDensityMatrix.coeffRef(i, j) * aDensityMatrix.coeffRef(k, l);
                    bbG[i * N * N * N + j * N * N + k * N + l] = bDensityMatrix.coeffRef(i, j) * bDensityMatrix.coeffRef(k, l);
                }
            }
        }
    } 

    double EImp = 0.0;

    for (int i = 0; i < FragPos.size(); i++)
    {
        int iIdx = FragPos[i];
        for (int j = 0; j < N; j++)
        {
            EImp += 0.5 * aDensityMatrix.coeffRef(iIdx, j) * (aOEI[iIdx * N + j] + aOEIPlusCore[iIdx * N + j])
                  + 0.5 * bDensityMatrix.coeffRef(iIdx, j) * (bOEI[iIdx * N + j] + bOEIPlusCore[iIdx * N + j]);
            for (int k = 0; k < N; k++)
            {
                for (int l = 0; l < N; l++)
                {
                    EImp += 0.5 * 2.0 * aaG[iIdx * N * N * N + j * N * N + k * N + l] * (aaTEI[iIdx * N * N * N + j * N * N + k * N + l] - aaTEI[iIdx * N * N * N + l * N * N + k * N + j])
                          + 1.0 * 2.0 * abG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[iIdx * N * N * N + j * N * N + k * N + l] + 1.0 * 2.0 * baG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[k * N * N * N + l * N * N + iIdx * N + j]
                          + 0.5 * 2.0 * bbG[iIdx * N * N * N + j * N * N + k * N + l] * (bbTEI[iIdx * N * N * N + j * N * N + k * N + l] - bbTEI[iIdx * N * N * N + l * N * N + k * N + j]);
                }
            }
        }
    }

    return EImp;
}

double CalcSCFImpurityEnergy(Eigen::MatrixXd aDensityMatrix, Eigen::MatrixXd bDensityMatrix, std::vector<int> aFragPos, std::vector<int> bFragPos, double *aOEI, double *bOEI, double *aOEIPlusCore, double *bOEIPlusCore, double *aaTEI, double *abTEI, double *bbTEI)
{
    int N = aDensityMatrix.rows();
    int N4 = N * N * N * N;
    std::vector<double> aaG(N4), abG(N4), baG(N4), bbG(N4);
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
        {
            for (int k = 0; k < N; k++)
            {
                for (int l = 0; l < N; l++)
                {
                    aaG[i * N * N * N + j * N * N + k * N + l] = aDensityMatrix.coeffRef(i, j) * aDensityMatrix.coeffRef(k, l);
                    abG[i * N * N * N + j * N * N + k * N + l] = aDensityMatrix.coeffRef(i, j) * bDensityMatrix.coeffRef(k, l);
                    baG[i * N * N * N + j * N * N + k * N + l] = bDensityMatrix.coeffRef(i, j) * aDensityMatrix.coeffRef(k, l);
                    bbG[i * N * N * N + j * N * N + k * N + l] = bDensityMatrix.coeffRef(i, j) * bDensityMatrix.coeffRef(k, l);
                }
            }
        }
    } 

    double EImp = 0.0;

    for (int i = 0; i < aFragPos.size(); i++)
    {
        int iIdx = aFragPos[i];
        for (int j = 0; j < N; j++)
        {
            EImp += 0.5 * aDensityMatrix.coeffRef(iIdx, j) * (aOEI[iIdx * N + j] + aOEIPlusCore[iIdx * N + j]);
            for (int k = 0; k < N; k++)
            {
                for (int l = 0; l < N; l++)
                {
                    // EImp += 0.5 * 2.0 * aaG[iIdx * N * N * N + j * N * N + k * N + l] * (aaTEI[iIdx * N * N * N + j * N * N + k * N + l] - aaTEI[iIdx * N * N * N + l * N * N + k * N + j])
                    //      + 1.0 * 1.0 * abG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[iIdx * N * N * N + j * N * N + k * N + l] + 1.0 * 1.0 * baG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[k * N * N * N + l * N * N + iIdx * N + j];
                    EImp += aaG[iIdx * N * N * N + j * N * N + k * N + l] * aaTEI[iIdx * N * N * N + j * N * N + k * N + l]
                         + 0.5 * abG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[iIdx * N * N * N + j * N * N + k * N + l];
                }
            }
        }
    }

    for (int i = 0; i < bFragPos.size(); i++)
    {
        int iIdx = bFragPos[i];
        for (int j = 0; j < N; j++)
        {
            EImp += 0.5 * bDensityMatrix.coeffRef(iIdx, j) * (bOEI[iIdx * N + j] + bOEIPlusCore[iIdx * N + j]);
            for (int k = 0; k < N; k++)
            {
                for (int l = 0; l < N; l++)
                {
                    // EImp += 0.5 * 2.0 * bbG[iIdx * N * N * N + j * N * N + k * N + l] * (bbTEI[iIdx * N * N * N + j * N * N + k * N + l] - bbTEI[iIdx * N * N * N + l * N * N + k * N + j])
                    //       + 1.0 * 1.0 * abG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[iIdx * N * N * N + j * N * N + k * N + l] + 1.0 * 1.0 * baG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[k * N * N * N + l * N * N + iIdx * N + j];
                    EImp += bbG[iIdx * N * N * N + j * N * N + k * N + l] * bbTEI[iIdx * N * N * N + j * N * N + k * N + l]
                         + 0.5 * abG[iIdx * N * N * N + j * N * N + k * N + l] * abTEI[iIdx * N * N * N + j * N * N + k * N + l];
                }
            }
        }
    }

    return EImp;
}