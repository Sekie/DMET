#include <iostream>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/Eigenvalues>
#include <vector>
#include <cmath>
#include <tuple>
#include <fstream>
#include <map>
#include <stdlib.h> 
#include <algorithm> // std::sort
#include <iomanip>
#include <queue>
#include <ctime>
#include "Bootstrap.h"
#include "Functions.h"
#include "NewtonRaphson.h"
#include "FCI.h"
#include "Fragmenting.h"
#include <utility>

// This is a debug function for the time being
//void Bootstrap::debugInit(InputObj Inp, std::ofstream &OutStream)
//{
//	Output = &OutStream;
//
//	NumAO = Inp.NumAO;
//	NumOcc = Inp.NumOcc;
//	NumFrag = Inp.FragmentOrbitals.size();
//	FragmentOrbitals = Inp.FragmentOrbitals;
//	EnvironmentOrbitals = Inp.EnvironmentOrbitals;
//	Input = Inp;
//
//	for (int x = 0; x < NumFrag; x++)
//	{
//		std::vector< std::tuple <int , int, double > > FragmentBEPotential;
//		FragmentBEPotential.push_back(std::tuple< int, int, double >((x + 1) % NumFrag, (x + 2) % NumFrag, 0.0));
//		FragmentBEPotential.push_back(std::tuple< int, int, double >((x + NumFrag - 1) % NumFrag, (x + NumFrag) % NumFrag, 0.0));
//		BEPotential.push_back(FragmentBEPotential);
//
//		std::vector< int >  CenterPos;
//		if (x == NumFrag - 2)
//		{
//			CenterPos.push_back(2);
//		}
//		else if (x == NumFrag - 1)
//		{
//			CenterPos.push_back(0);
//		}
//		else
//		{
//			CenterPos.push_back(1);
//		}
//		// CenterPos.push_back(1);
//		BECenterPosition.push_back(CenterPos);
//	}
//
//	FragState = std::vector<int>(NumFrag);
//	BathState = std::vector<int>(NumFrag);
//	// H10
//	FragState[0] = 3;
//	FragState[1] = 3;
//	FragState[2] = 3;
//	FragState[3] = 3;
//	FragState[4] = 3;
//	FragState[5] = 3;
//	FragState[6] = 3;
//	FragState[7] = 3;
//	FragState[8] = 3;
//	FragState[9] = 3;
//
//	BathState[0] = 1;
//	BathState[1] = 1;
//	BathState[2] = 1;
//	BathState[3] = 1;
//	BathState[4] = 1;
//	BathState[5] = 1;
//	BathState[6] = 1;
//	BathState[7] = 1;
//	BathState[8] = 1;
//	BathState[9] = 1;
//
//	isTS = true;
//
//	// Will be important to initialize this immediately when BE is implemented.
//	for (int x = 0; x < NumFrag; x++)
//	{
//		NumConditions += BEPotential[x].size();
//		NumFragCond.push_back(BEPotential[x].size());
//	}
//}

void Bootstrap::InitFromFragmenting(Fragmenting Frag, std::ofstream &OutStream)
{
	Output = &OutStream;
	NumFrag = Frag.MatchingConditions.size();
	TrueNumFrag = NumFrag;
	NumFragCond.clear();
	BEPotential.clear();
	NumConditions = 0;
	for (int x = 0; x < Frag.MatchingConditions.size(); x++)
	{
		NumFragCond.push_back(Frag.MatchingConditions[x].size());
		NumConditions += NumFragCond[x];
		std::vector< std::tuple<int, int, int, int, int, double, bool, bool> > tmpVec;
		for (int k = 0; k < Frag.MatchingConditions[x].size(); k++)
		{
			tmpVec.push_back(std::make_tuple(std::get<0>(Frag.MatchingConditions[x][k]), std::get<1>(Frag.MatchingConditions[x][k]), std::get<2>(Frag.MatchingConditions[x][k]), std::get<3>(Frag.MatchingConditions[x][k]),
			std::get<4>(Frag.MatchingConditions[x][k]), 0.0, std::get<5>(Frag.MatchingConditions[x][k]), std::get<6>(Frag.MatchingConditions[x][k])));
		}
		BEPotential.push_back(tmpVec);
	}

	aBECenterPosition = bBECenterPosition = Frag.CenterPosition;
	OrbitalAnalogs = Frag.OrbitalAnalog;
}

void Bootstrap::PrintOneRDMs(std::vector< std::vector<Eigen::MatrixXd> > OneRDMs)
{
	for (int x = 0; x < OneRDMs.size(); x++)
	{
		*Output << "BE-DMET: One RDM for Fragment " << x << " is\n" << OneRDMs[x][FragState[x]] << std::endl;
	}
}

int Bootstrap::OrbitalToReducedIndex(int Orbital, int FragIndex, bool Alpha)
{
	int RedIdx;
	if (Alpha)
	{
		for (int i = 0; i < aFragPos[FragIndex].size(); i++)
		{
			int Pos = aFragPos[FragIndex][i];
			int Orb = ReducedIndexToOrbital(Pos, Input, FragIndex, Alpha);
			if (Orb == Orbital) RedIdx = Pos;
		}

	}
	else
	{
		for (int i = 0; i < bFragPos[FragIndex].size(); i++)
		{
			int Pos = bFragPos[FragIndex][i];
			int Orb = ReducedIndexToOrbital(Pos, Input, FragIndex, Alpha);
			if (Orb == Orbital) RedIdx = Pos;
		}
	}
	return RedIdx;
}

double Bootstrap::CalcCostChemPot(std::vector< std::vector<Eigen::MatrixXd> > Frag1RDMs, std::vector< std::vector< int > > BECenter, std::vector<int> FragSt, InputObj &Inp)
{
    double CF = 0;
    for(int x = 0; x < Frag1RDMs.size(); x++) // sum over fragments
    {
        std::vector< int > FragPos;
        std::vector< int > BathPos;
        GetCASPos(Inp, x , FragPos, BathPos);
        for(int i = 0; i < BECenter[x].size(); i++) // sum over diagonal matrix elements belonging to the fragment orbitals.
        {
            CF += Frag1RDMs[x][FragSt[x]](FragPos[BECenter[x][i]], FragPos[BECenter[x][i]]);
        }
    }
	if (isTS)
	{
		CF *= TrueNumFrag;
	}
    CF -= Inp.NumElectrons;
    CF = CF * CF;
    return CF;
}

std::vector<double> Bootstrap::CalcCostChemPot(std::vector<Eigen::MatrixXd> aFrag1RDMs, std::vector<Eigen::MatrixXd> bFrag1RDMs, std::vector< std::vector< int > > aBECenter, std::vector< std::vector< int > > bBECenter)
{
    std::vector<double> CF(2);
	double aCF = 0.0;
	double bCF = 0.0;
	
    for(int x = 0; x < aFrag1RDMs.size(); x++) // sum over fragments
    {
        for (int i = 0; i < aBECenter[x].size(); i++) // sum over diagonal matrix elements belonging to the fragment orbitals.
        {
			int CenterIdx = OrbitalToReducedIndex(aBECenter[x][i], x, true);
            aCF += aFrag1RDMs[x].coeffRef(CenterIdx, CenterIdx);
        }
		for (int i = 0; i < bBECenter[x].size(); i++)
		{
			int CenterIdx = OrbitalToReducedIndex(bBECenter[x][i], x, false);
            bCF += bFrag1RDMs[x].coeffRef(CenterIdx, CenterIdx);
		}
    }
	if (isTS)
	{
		aCF *= TrueNumFrag;
		bCF *= TrueNumFrag;
	}
    aCF -= Input.aNumElectrons;
	bCF -= Input.bNumElectrons;
	// aCF = aCF * aCF;
	// bCF = bCF * bCF;
    CF[0] = aCF;
	CF[1] = bCF;
    return CF;
}

std::vector<double> Bootstrap::CalcCostChemPot()
{
    std::vector<double> CF(2);
	double aCF = 0.0;
	double bCF = 0.0;

    for(int x = 0; x < NumFrag; x++) // sum over fragments
    {
        for (int i = 0; i < aBECenterPosition[x].size(); i++) // sum over diagonal matrix elements belonging to the fragment orbitals.
        {
			int CenterIdx = OrbitalToReducedIndex(aBECenterPosition[x][i], x, true);
            aCF += FCIs[x].aOneRDMs[FragState[x]].coeffRef(CenterIdx, CenterIdx);
        }
		for (int i = 0; i < bBECenterPosition[x].size(); i++)
		{
			int CenterIdx = OrbitalToReducedIndex(bBECenterPosition[x][i], x, false);
            bCF += FCIs[x].bOneRDMs[FragState[x]].coeffRef(CenterIdx, CenterIdx);
		}
    }
	if (isTS)
	{
		aCF *= TrueNumFrag;
		bCF *= TrueNumFrag;
	}
    aCF -= Input.aNumElectrons;
	bCF -= Input.bNumElectrons;
	// aCF = aCF * aCF;
	// bCF = bCF * bCF;
    CF[0] = aCF;
	CF[1] = bCF;
    return CF;
}

Eigen::MatrixXd Bootstrap::CalcJacobianChemPot(Eigen::VectorXd &Loss, double aMu, double bMu)
{
	Eigen::MatrixXd JMu(2, 2);
	std::vector<double> L0;
	std::vector<Eigen::MatrixXd> aOneRDMs0, bOneRDMs0;
	CollectRDM(aOneRDMs0, bOneRDMs0, BEPotential, aMu, bMu);
	L0 = CalcCostChemPot(aOneRDMs0, bOneRDMs0, aBECenterPosition, bBECenterPosition);

	for (int i = 0; i < 2; i++)
	{
		Eigen::VectorXd MuP(2), MuM(2);
		MuP[0] = MuM[0] = aMu;
		MuP[1] = MuM[1] = bMu;
		
		MuP[i] += dMu;
		// MuM[i] -= dMu;

		std::vector<double> LP, LM;
		std::vector<Eigen::MatrixXd> aOneRDMsP, bOneRDMsP, aOneRDMsM, bOneRDMsM;
		CollectRDM(aOneRDMsP, bOneRDMsP, BEPotential, MuP[0], MuP[1]);
		LP = CalcCostChemPot(aOneRDMsP, bOneRDMsP, aBECenterPosition, bBECenterPosition);
		CollectRDM(aOneRDMsM, bOneRDMsM, BEPotential, MuM[0], MuM[1]);
		LM = CalcCostChemPot(aOneRDMsM, bOneRDMsM, aBECenterPosition, bBECenterPosition);

		double dLadMu = (LP[0] - LM[0]) / (dMu);
		double dLbdMu = (LP[1] - LM[1]) / (dMu);

		JMu(0, i) = dLadMu;
		JMu(1, i) = dLbdMu;
	}

	Loss[0] = L0[0];
	Loss[1] = L0[1];
	return JMu;
}

std::vector<double> Bootstrap::CalcCostLambda(std::vector<Eigen::MatrixXd> aOneRDMRef, std::vector<Eigen::MatrixXd> bOneRDMRef, std::vector< std::vector<double> > aaTwoRDMRef, std::vector< std::vector<double> > abTwoRDMRef, std::vector< std::vector<double> > bbTwoRDMRef,
                                 Eigen::MatrixXd aOneRDMIter, Eigen::MatrixXd bOneRDMIter, std::vector<double> aaTwoRDMIter, std::vector<double> abTwoRDMIter, std::vector<double> bbTwoRDMIter, int FragmentIndex)
{
	std::vector<double> Loss;
	for (int i = 0; i < BEPotential[FragmentIndex].size(); i++)
	{
		double PRef, PIter;

		bool isOEI = false;
		if (std::get<3>(BEPotential[FragmentIndex][i]) == -1) isOEI = true;
		if (isOEI)
		{
			int Ind1Ref = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind2Ref = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<6>(BEPotential[FragmentIndex][i]));
			if (isTS)
			{
				int Orb1 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<1>(BEPotential[FragmentIndex][i]))];
				int Orb2 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<2>(BEPotential[FragmentIndex][i]))];
				Ind1Ref = OrbitalToReducedIndex(Orb1, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
				Ind2Ref = OrbitalToReducedIndex(Orb2, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
			}

			int Ind1Iter = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind2Iter = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));

			if (MatchFullP)
			{
				int bInd1Ref = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), false);
				int bInd2Ref = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), false);
				if (isTS)
				{
					int Orb1 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<1>(BEPotential[FragmentIndex][i]))];
					int Orb2 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<2>(BEPotential[FragmentIndex][i]))];
					bInd1Ref = OrbitalToReducedIndex(Orb1, FragmentIndex, false);
					bInd2Ref = OrbitalToReducedIndex(Orb2, FragmentIndex, false);
				}

				int bInd1Iter = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), FragmentIndex, false);
				int bInd2Iter = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), FragmentIndex, false);

				// Eigen::MatrixXd OneRDMRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])] + bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])];
				// Eigen::MatrixXd OneRDMIter = aOneRDMIter + bOneRDMIter;

				if (isTS)
				{
					PRef = aOneRDMRef[FragmentIndex].coeffRef(Ind1Ref, Ind2Ref) + bOneRDMRef[FragmentIndex].coeffRef(bInd1Ref, bInd2Ref);
				}
				else
				{
					PRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].coeffRef(Ind1Ref, Ind2Ref) + bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].coeffRef(bInd1Ref, bInd2Ref);
				}
				
				PIter = aOneRDMIter.coeffRef(Ind1Iter, Ind2Iter) + bOneRDMIter.coeffRef(bInd1Iter, bInd2Iter);
			}
			else
			{
				if (std::get<6>(BEPotential[FragmentIndex][i]))
				{
					if (isTS) PRef = aOneRDMRef[FragmentIndex].coeffRef(Ind1Ref, Ind2Ref);
					else PRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].coeffRef(Ind1Ref, Ind2Ref);
					PIter = aOneRDMIter.coeffRef(Ind1Iter, Ind2Iter);
				}
				else
				{
					if (isTS) PRef = bOneRDMRef[FragmentIndex].coeffRef(Ind1Ref, Ind2Ref);
					else PRef = bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].coeffRef(Ind1Ref, Ind2Ref);
					PIter = bOneRDMIter.coeffRef(Ind1Iter, Ind2Iter);
				}
			}
		}
		else
		{
			int Ind1Ref = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind2Ref = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind3Ref = OrbitalToReducedIndex(std::get<3>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<7>(BEPotential[FragmentIndex][i]));
			int Ind4Ref = OrbitalToReducedIndex(std::get<4>(BEPotential[FragmentIndex][i]), std::get<0>(BEPotential[FragmentIndex][i]), std::get<7>(BEPotential[FragmentIndex][i]));
			if (isTS)
			{
				int Orb1 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<1>(BEPotential[FragmentIndex][i]))];
				int Orb2 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<2>(BEPotential[FragmentIndex][i]))];
				int Orb3 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<3>(BEPotential[FragmentIndex][i]))];
				int Orb4 = OrbitalAnalogs[std::make_pair(std::get<0>(BEPotential[FragmentIndex][i]), std::get<4>(BEPotential[FragmentIndex][i]))];
				Ind1Ref = OrbitalToReducedIndex(Orb1, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
				Ind2Ref = OrbitalToReducedIndex(Orb2, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
				Ind3Ref = OrbitalToReducedIndex(Orb3, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
				Ind4Ref = OrbitalToReducedIndex(Orb4, FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
			}

			int Ind1Iter = OrbitalToReducedIndex(std::get<1>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind2Iter = OrbitalToReducedIndex(std::get<2>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<6>(BEPotential[FragmentIndex][i]));
			int Ind3Iter = OrbitalToReducedIndex(std::get<3>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<7>(BEPotential[FragmentIndex][i]));
			int Ind4Iter = OrbitalToReducedIndex(std::get<4>(BEPotential[FragmentIndex][i]), FragmentIndex, std::get<7>(BEPotential[FragmentIndex][i]));

			if (std::get<6>(BEPotential[FragmentIndex][i]) && std::get<7>(BEPotential[FragmentIndex][i]))
			{
				if (isTS)
				{
					int NRef = aOneRDMRef[FragmentIndex].rows();
					PRef = aaTwoRDMRef[FragmentIndex][Ind1Ref * NRef * NRef * NRef + Ind2Ref * NRef * NRef + Ind3Ref * NRef + Ind4Ref];
				}
				else
				{
					int NRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					PRef = aaTwoRDMRef[std::get<0>(BEPotential[FragmentIndex][i])][Ind1Ref * NRef * NRef * NRef + Ind2Ref * NRef * NRef + Ind3Ref * NRef + Ind4Ref];
				}
				int NIter = aOneRDMIter.rows();
				PIter = aaTwoRDMIter[Ind1Iter * NIter * NIter * NIter + Ind2Iter * NIter * NIter + Ind3Iter * NIter + Ind4Iter];
			}
			else if (!std::get<6>(BEPotential[FragmentIndex][i]) && !std::get<7>(BEPotential[FragmentIndex][i]))
			{
				if (isTS)
				{
					int NRef = bOneRDMRef[FragmentIndex].rows();
					PRef = bbTwoRDMRef[FragmentIndex][Ind1Ref * NRef * NRef * NRef + Ind2Ref * NRef * NRef + Ind3Ref * NRef + Ind4Ref];
				}
				else
				{
					int NRef = bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					PRef = bbTwoRDMRef[std::get<0>(BEPotential[FragmentIndex][i])][Ind1Ref * NRef * NRef * NRef + Ind2Ref * NRef * NRef + Ind3Ref * NRef + Ind4Ref];
				}
				int NIter = bOneRDMIter.rows();
				PIter = bbTwoRDMIter[Ind1Iter * NIter * NIter * NIter + Ind2Iter * NIter * NIter + Ind3Iter * NIter + Ind4Iter];
			}
			else
			{
				if (isTS)
				{
					int aNRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					int bNRef = bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					PRef = abTwoRDMRef[std::get<0>(BEPotential[FragmentIndex][i])][Ind1Ref * aNRef * bNRef * bNRef + Ind2Ref * bNRef * bNRef + Ind3Ref * bNRef + Ind4Ref];
				}
				else
				{
					int aNRef = aOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					int bNRef = bOneRDMRef[std::get<0>(BEPotential[FragmentIndex][i])].rows();
					PRef = abTwoRDMRef[std::get<0>(BEPotential[FragmentIndex][i])][Ind1Ref * aNRef * bNRef * bNRef + Ind2Ref * bNRef * bNRef + Ind3Ref * bNRef + Ind4Ref];
				}
				int aNIter = aOneRDMIter.rows();
				int bNIter = bOneRDMIter.rows();
				PIter = bbTwoRDMIter[Ind1Iter * aNIter * bNIter * bNIter + Ind2Iter * bNIter * bNIter + Ind3Iter * bNIter + Ind4Iter];
			}
		}
		Loss.push_back(PRef - PIter);
	}
	return Loss;
}

void Bootstrap::CollectRDM(std::vector< Eigen::MatrixXd > &aOneRDMs, std::vector< Eigen::MatrixXd > &bOneRDMs, std::vector< std::vector<double> > &aaTwoRDMs, std::vector< std::vector<double> > &abTwoRDMs, std::vector< std::vector<double> > &bbTwoRDMs,
                           std::vector< std::vector< std::tuple< int, int, int, int, int, double, bool, bool > > > BEPot, double aMu, double bMu)
{
	for (int x = 0; x < NumFrag; x++)
	{
		FCI xFCI(FCIsBase[x]);
		
		xFCI.AddChemicalPotentialGKLC(aBECenterIndex[x], bBECenterIndex[x], aMu, bMu);
		for (int i = 0; i < BEPot[x].size(); i++)
		{
			bool OEIPotential = false;
			if (std::get<3>(BEPot[x][i]) == -1) OEIPotential = true;

			if (OEIPotential)
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));

				xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPot[x][i]), std::get<6>(BEPot[x][i]));
				if (MatchFullP)
				{
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPot[x][i]), false);
				}
			}
			else
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				int Ind3 = OrbitalToReducedIndex(std::get<3>(BEPot[x][i]), x, std::get<7>(BEPot[x][i]));
				int Ind4 = OrbitalToReducedIndex(std::get<4>(BEPot[x][i]), x, std::get<7>(BEPot[x][i]));

				xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), std::get<6>(BEPot[x][i]), std::get<7>(BEPot[x][i]));
				if (MatchFullP)
				{
					Ind3 = OrbitalToReducedIndex(std::get<3>(BEPot[x][i]), x, false);
					Ind4 = OrbitalToReducedIndex(std::get<4>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), true, false);
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), false, false);
				}
			}
		}
		if (x == 0) xFCI.PrintERI(true);
		if (doDavidson) xFCI.runFCI();
		else xFCI.DirectFCI();
		xFCI.getSpecificRDM(FragState[x], true);
		aOneRDMs.push_back(xFCI.aOneRDMs[FragState[x]]);
		bOneRDMs.push_back(xFCI.bOneRDMs[FragState[x]]);
		aaTwoRDMs.push_back(xFCI.aaTwoRDMs[FragState[x]]);
		abTwoRDMs.push_back(xFCI.abTwoRDMs[FragState[x]]);
		bbTwoRDMs.push_back(xFCI.bbTwoRDMs[FragState[x]]);
	}
}

void Bootstrap::CollectRDM(std::vector< Eigen::MatrixXd > &aOneRDMs, std::vector< Eigen::MatrixXd > &bOneRDMs,
                           std::vector< std::vector< std::tuple< int, int, int, int, int, double, bool, bool > > > BEPot, double aMu, double bMu)
{
	for (int x = 0; x < NumFrag; x++)
	{
		FCI xFCI(FCIsBase[x]);
		
		xFCI.AddChemicalPotentialGKLC(aBECenterIndex[x], bBECenterIndex[x], aMu, bMu);
		for (int i = 0; i < BEPot[x].size(); i++)
		{
			bool OEIPotential = false;
			if (std::get<3>(BEPot[x][i]) == -1) OEIPotential = true;

			if (OEIPotential)
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, std::get<5>(BEPot[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				
				xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPot[x][i]), std::get<6>(BEPot[x][i]));
				if (MatchFullP)
				{
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPot[x][i]), false);
				}
			}
			else
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, std::get<6>(BEPot[x][i]));
				int Ind3 = OrbitalToReducedIndex(std::get<3>(BEPot[x][i]), x, std::get<7>(BEPot[x][i]));
				int Ind4 = OrbitalToReducedIndex(std::get<4>(BEPot[x][i]), x, std::get<7>(BEPot[x][i]));

				xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), std::get<6>(BEPot[x][i]), std::get<7>(BEPot[x][i]));
				if (MatchFullP)
				{
					Ind3 = OrbitalToReducedIndex(std::get<3>(BEPot[x][i]), x, false);
					Ind4 = OrbitalToReducedIndex(std::get<4>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), true, false);
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPot[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPot[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPot[x][i]), false, false);
				}
			}
		}
		// if (x == 0) xFCI.PrintERI(false);
		if (doDavidson) xFCI.runFCI();
		else xFCI.DirectFCI();
		xFCI.getSpecificRDM(FragState[x], false);
		aOneRDMs.push_back(xFCI.aOneRDMs[FragState[x]]);
		bOneRDMs.push_back(xFCI.bOneRDMs[FragState[x]]);
	}
}

void Bootstrap::UpdateFCIs()
{
	FCIs.clear();
	std::vector<FCI>().swap(FCIs);
	for (int x = 0; x < NumFrag; x++)
	{
		FCI xFCI(FCIsBase[x]); // First, reset FCI
		xFCI.AddChemicalPotentialGKLC(aBECenterIndex[x], bBECenterIndex[x], aChemicalPotential, bChemicalPotential);
		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			bool isOEI = (std::get<3>(BEPotential[x][i]) == -1);
			if (isOEI)
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));

				xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPotential[x][i]), std::get<6>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPotential[x][i]), false);
				}
			}
			else
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));
				int Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));

				xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), std::get<6>(BEPotential[x][i]), std::get<7>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, false);
					Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), true, false);
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), false, false);
				}
			}
		}
		if (doDavidson) xFCI.runFCI();
		else xFCI.DirectFCI();
		xFCI.getSpecificRDM(FragState[x], true);
		FCIs.push_back(xFCI);
	}
}

void Bootstrap::UpdateFCIsE()
{
	FCIs.clear();
	std::vector<FCI>().swap(FCIs);
	for (int x = 0; x < NumFrag; x++)
	{
		FCI xFCI(FCIsBase[x]); // First, reset FCI
		xFCI.AddChemicalPotentialGKLC(aBECenterIndex[x], bBECenterIndex[x], aChemicalPotential / 2.0, bChemicalPotential / 2.0);
		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			bool isOEI = (std::get<3>(BEPotential[x][i]) == -1);
			if (isOEI)
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));

				xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPotential[x][i]), std::get<6>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, std::get<5>(BEPotential[x][i]), false);
				}
			}
			else
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));
				int Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));

				xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), std::get<6>(BEPotential[x][i]), std::get<7>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, false);
					Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), true, false);
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCI.AddPotential(Ind1, Ind2, Ind3, Ind4, std::get<5>(BEPotential[x][i]), false, false);
				}
			}
		}
		if (doDavidson) xFCI.runFCI();
		else xFCI.DirectFCI();
		xFCI.getSpecificRDM(FragState[x], true);
		FCIs.push_back(xFCI);
	}
}

std::vector< double > Bootstrap::FragmentLoss(std::vector< std::vector<Eigen::MatrixXd> > DensityReference, std::vector<Eigen::MatrixXd> IterDensity, int FragmentIndex)
{
	std::vector< double > AllLosses;
	for (int MatchedOrbital = 0; MatchedOrbital < BEPotential[FragmentIndex].size(); MatchedOrbital++)
	{
		std::vector< int > FragPosImp, BathPosImp, FragPosBath, BathPosBath;
		GetCASPos(Input, FragmentIndex, FragPosImp, BathPosImp);
		GetCASPos(Input, std::get<0>(BEPotential[FragmentIndex][MatchedOrbital]), FragPosBath, BathPosBath);

		int PElementImp = 0;
		for (int i = 0; i < Input.FragmentOrbitals[FragmentIndex].size(); i++)
		{
			if (Input.FragmentOrbitals[FragmentIndex][i] == std::get<1>(BEPotential[FragmentIndex][MatchedOrbital]))
			{
				break;
			}
			PElementImp++;
		}
		int PElementBath = 0;
		for (int i = 0; i < Input.FragmentOrbitals[std::get<0>(BEPotential[FragmentIndex][MatchedOrbital])].size(); i++)
		{
			if (Input.FragmentOrbitals[std::get<0>(BEPotential[FragmentIndex][MatchedOrbital])][i] == std::get<1>(BEPotential[FragmentIndex][MatchedOrbital]))
			{
				break;
			}
			PElementBath++;
		}
		double Loss = DensityReference[std::get<0>(BEPotential[FragmentIndex][MatchedOrbital])][FragState[std::get<0>(BEPotential[FragmentIndex][MatchedOrbital])]].coeffRef(FragPosBath[PElementBath], FragPosBath[PElementBath]) 
			- IterDensity[FragState[FragmentIndex]].coeffRef(FragPosImp[PElementImp], FragPosImp[PElementImp]);
		Loss = Loss * Loss;
		AllLosses.push_back(Loss);
	}
	return AllLosses;
}

Eigen::MatrixXd Bootstrap::CalcJacobian(Eigen::VectorXd &f)
{
	std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;
	std::vector< std::vector<double> > aaTwoRDMs, abTwoRDMs, bbTwoRDMs, tmpVecVecDouble;
	for (int x = 0; x < NumFrag; x++)
	{
		aOneRDMs.push_back(FCIs[x].aOneRDMs[FragState[x]]);
		bOneRDMs.push_back(FCIs[x].bOneRDMs[FragState[x]]);
		aaTwoRDMs.push_back(FCIs[x].aaTwoRDMs[FragState[x]]);
		abTwoRDMs.push_back(FCIs[x].abTwoRDMs[FragState[x]]);
		bbTwoRDMs.push_back(FCIs[x].bbTwoRDMs[FragState[x]]);
		// std::cout << x << "\na\n" << FCIs[x].aOneRDMs[FragState[x]] << "\nb\n" << FCIs[x].bOneRDMs[FragState[x]] << std::endl;
	}

	// double LMu = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition, FragState);
	// std::vector<Eigen::MatrixXd> aOneRDMsPlusdMu, bOneRDMsPlusdMu;
	// CollectRDM(aOneRDMsPlusdMu, bOneRDMsPlusdMu, tmpVecVecDouble, tmpVecVecDouble, tmpVecVecDouble, BEPotential, ChemicalPotential + dMu);
	// double LMuPlus = CalcCostChemPot(aOneRDMsPlusdMu, bOneRDMsPlusdMu, aBECenterPosition, bBECenterPosition, FragState);

	f = Eigen::VectorXd::Zero(NumConditions);
	Eigen::MatrixXd J = Eigen::MatrixXd::Zero(NumConditions, NumConditions);

	// f[NumConditions] = LMu;
	// J(NumConditions, NumConditions) = (LMuPlus - LMu) / dMu;

	// Jacobian is 
	// [ df1/dx1, ..., df1/dxn ]
	// [ ..................... ]
	// [ dfn/dx1, ..., dfn/dxn ]
	// fi are the squared losses at the different matched orbitals
	// xi are the different lambdas used to match each orbital

	int JCol = 0;
	int fCount = 0;
	for (int x = 0; x < NumFrag; x++)
	{
		std::vector<double> LossesBase = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, aOneRDMs[x], bOneRDMs[x], aaTwoRDMs[x], abTwoRDMs[x], bbTwoRDMs[x], x);

		int JRow = 0;
		for (int j = 0; j < x; j++)
		{
			JRow += NumFragCond[j];
		}

		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			bool isOEI = (std::get<3>(BEPotential[x][i]) == -1);

			// Collect all the density matrices for this iteration.
			FCI xFCIp(FCIs[x]);
			FCI xFCIm(FCIs[x]);

			if (isOEI)
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));

				xFCIp.AddPotential(Ind1, Ind2,  dLambda, std::get<6>(BEPotential[x][i]));
				xFCIm.AddPotential(Ind1, Ind2, -dLambda, std::get<6>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCIp.AddPotential(Ind1, Ind2,  dLambda, false);
					xFCIm.AddPotential(Ind1, Ind2, -dLambda, false);
				}
			}
			else
			{
				int Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, std::get<6>(BEPotential[x][i]));
				int Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));
				int Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, std::get<7>(BEPotential[x][i]));

				xFCIp.AddPotential(Ind1, Ind2, Ind3, Ind4,  dLambda, std::get<6>(BEPotential[x][i]), std::get<7>(BEPotential[x][i]));
				xFCIm.AddPotential(Ind1, Ind2, Ind3, Ind4, -dLambda, std::get<6>(BEPotential[x][i]), std::get<7>(BEPotential[x][i]));
				if (MatchFullP)
				{
					Ind3 = OrbitalToReducedIndex(std::get<3>(BEPotential[x][i]), x, false);
					Ind4 = OrbitalToReducedIndex(std::get<4>(BEPotential[x][i]), x, false);
					xFCIp.AddPotential(Ind1, Ind2, Ind3, Ind4,  dLambda, true, false);
					xFCIm.AddPotential(Ind1, Ind2, Ind3, Ind4, -dLambda, true, false);
					Ind1 = OrbitalToReducedIndex(std::get<1>(BEPotential[x][i]), x, false);
					Ind2 = OrbitalToReducedIndex(std::get<2>(BEPotential[x][i]), x, false);
					xFCIp.AddPotential(Ind1, Ind2, Ind3, Ind4,  dLambda, false, false);
					xFCIm.AddPotential(Ind1, Ind2, Ind3, Ind4, -dLambda, false, false);
				}
			}
			if (doDavidson) xFCIp.runFCI();
			else xFCIp.DirectFCI();
			xFCIp.getSpecificRDM(FragState[x], !isOEI);
			if (doDavidson) xFCIm.runFCI();
			else xFCIm.DirectFCI();
			xFCIm.getSpecificRDM(FragState[x], !isOEI);

			std::vector<double> LossesPlus;
			std::vector<double> LossesMins;
			std::vector<Eigen::MatrixXd> aOneRDMsP, bOneRDMsP, aOneRDMsM, bOneRDMsM;
			std::vector< std::vector<double> > aaTwoRDMsP, abTwoRDMsP, bbTwoRDMsP, aaTwoRDMsM, abTwoRDMsM, bbTwoRDMsM;

			aOneRDMsP = aOneRDMsM = aOneRDMs;
			bOneRDMsP = bOneRDMsM = bOneRDMs;
			aaTwoRDMsP = aaTwoRDMsM = aaTwoRDMs;
			abTwoRDMsP = abTwoRDMsM = abTwoRDMs;
			bbTwoRDMsP = bbTwoRDMsM = bbTwoRDMs;

			aOneRDMsP[x] = xFCIp.aOneRDMs[FragState[x]];
			aOneRDMsM[x] = xFCIm.aOneRDMs[FragState[x]];
			bOneRDMsP[x] = xFCIp.bOneRDMs[FragState[x]];
			bOneRDMsM[x] = xFCIm.bOneRDMs[FragState[x]];
			if (!isOEI)
			{
				aaTwoRDMsP[x] = xFCIp.aaTwoRDMs[FragState[x]];
				aaTwoRDMsM[x] = xFCIm.aaTwoRDMs[FragState[x]];
				abTwoRDMsP[x] = xFCIp.abTwoRDMs[FragState[x]];
				abTwoRDMsM[x] = xFCIm.abTwoRDMs[FragState[x]];	
				bbTwoRDMsP[x] = xFCIp.bbTwoRDMs[FragState[x]];
				bbTwoRDMsM[x] = xFCIm.bbTwoRDMs[FragState[x]];	
			}

			if (isOEI)
			{
				std::vector<double> EmptyRDM;
				LossesPlus = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, xFCIp.aOneRDMs[FragState[x]], xFCIp.bOneRDMs[FragState[x]], EmptyRDM, EmptyRDM, EmptyRDM, x);
				LossesMins = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, xFCIm.aOneRDMs[FragState[x]], xFCIm.bOneRDMs[FragState[x]], EmptyRDM, EmptyRDM, EmptyRDM, x);
			}
			else
			{
				Eigen::MatrixXd EmptyRDM;
				LossesPlus = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, EmptyRDM, EmptyRDM, xFCIp.aaTwoRDMs[FragState[x]], xFCIp.abTwoRDMs[FragState[x]], xFCIp.bbTwoRDMs[FragState[x]], x);
				LossesMins = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, EmptyRDM, EmptyRDM, xFCIm.aaTwoRDMs[FragState[x]], xFCIm.abTwoRDMs[FragState[x]], xFCIm.bbTwoRDMs[FragState[x]], x);
			}
			// std::vector<Eigen::MatrixXd> aOneRDMsPlusdLambda = aOneRDMs;
			// std::vector<Eigen::MatrixXd> bOneRDMsPlusdLambda = bOneRDMs;
			// aOneRDMsPlusdLambda[x] = xFCI.aOneRDMs[FragState[x]];
			// bOneRDMsPlusdLambda[x] = xFCI.bOneRDMs[FragState[x]];
			// double dLmdl = CalcCostChemPot(aOneRDMsPlusdLambda, bOneRDMsPlusdLambda, aBECenterPosition, bBECenterPosition, FragState);
			// dLmdl = (dLmdl - LMu) / dLambda;
			
			// Fill in J
			for (int j = 0; j < LossesPlus.size(); j++)
			{
				J(JRow + j, JCol) = (LossesPlus[j] - LossesMins[j]) / (dLambda + dLambda);
				// J(JRow + j, JCol) = (LossesPlus[j] - LossesBase[j]) / (dLambda);
			}

			// Add in chemical potential portion.
			// std::vector<Eigen::MatrixXd> aSingleRDMPlus, bSingleRDMPlus;
			// std::vector< std::vector< int > > aSingleBECenter, bSingleBECenter;
			// aSingleRDMPlus.push_back(xFCI.aOneRDMs[FragState[x]]);
			// bSingleRDMPlus.push_back(xFCI.bOneRDMs[FragState[x]]);
			// aSingleBECenter.push_back(aBECenterPosition[x]);
			// bSingleBECenter.push_back(bBECenterPosition[x]);
			// double LMuPlus = CalcCostChemPot(aSingleRDMPlus, bSingleRDMPlus, aSingleBECenter, bSingleBECenter, FragState);
			// std::cout << "Mu Loss\n" << LMuPlus << "\t" << LMuByFrag[x] << std::endl;
			// J(J.rows() - 1, JCol) = dLmdl;

			JCol++;
		}

		// // Last column is derivative of each loss with respect to chemical potential.
		// // The last element of this column is already handled.
		// std::vector<double> Empty2RDM;
		// std::vector<double> LossesPlusMu = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, aOneRDMsPlusdMu[x], bOneRDMsPlusdMu[x], Empty2RDM, Empty2RDM, Empty2RDM, x);
		// for (int j = 0; j < LossesPlusMu.size(); j++)
		// {
		// 	J(JRow + j, NumConditions) = (LossesPlusMu[j] - LossesMinus[j]) / dMu;
		// }

		// Fill in f
		// The chemical potential loss is already filled in.
		for (int j = 0; j < LossesBase.size(); j++)
		{
			f[fCount] = LossesBase[j];
			fCount++;
		}
	}
	return J;
}

void Bootstrap::CollectSchmidt(std::vector<Eigen::MatrixXd> MFDensity, std::ofstream &Output)
{
	for (int x = 0; x < NumFrag; x++)
	{
		Eigen::MatrixXd RotMat = Eigen::MatrixXd::Zero(NumAO, NumAO);
		int NumEnvVirt = NumAO - NumOcc - FragmentOrbitals.size();
		SchmidtDecomposition(MFDensity[BathState[x]], RotMat, FragmentOrbitals[x], EnvironmentOrbitals[x], NumEnvVirt, Output);
		RotationMatrices.push_back(RotMat);
	}
}

void Bootstrap::CollectSchmidt(std::vector<Eigen::MatrixXd> aMFDensity, std::vector<Eigen::MatrixXd> bMFDensity, std::ofstream &Output)
{
	for (int x = 0; x < NumFrag; x++)
	{
		Eigen::MatrixXd aRotMat = Eigen::MatrixXd::Zero(NumAO, NumAO);
		Eigen::MatrixXd bRotMat = Eigen::MatrixXd::Zero(NumAO, NumAO);
		int aNumEnvVirt = NumAO - Input.aNumElectrons - FragmentOrbitals.size();
		int bNumEnvVirt = NumAO - Input.bNumElectrons - FragmentOrbitals.size();
		SchmidtDecomposition(aMFDensity[BathState[x]], aRotMat, FragmentOrbitals[x], EnvironmentOrbitals[x], aNumEnvVirt, Output);
		SchmidtDecomposition(bMFDensity[BathState[x]], bRotMat, FragmentOrbitals[x], EnvironmentOrbitals[x], bNumEnvVirt, Output);
		aRotationMatrices.push_back(aRotMat);
		bRotationMatrices.push_back(bRotMat);
	}
}

void Bootstrap::CollectInputs()
{
	for (int x = 0; x < NumFrag; x++)
	{
		InputObj FragInput = Input;
		FragInput.Integrals = RotateIntegrals(Input.Integrals, RotationMatrices[x]);
		Inputs.push_back(FragInput);
	}
}

void Bootstrap::VectorToBE(Eigen::VectorXd X)
{
	int xCount = 0;
	for (int x = 0; x < NumFrag; x++)
	{
		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			std::get<5>(BEPotential[x][i]) = X[xCount];
			xCount++;
		}
	}
}

Eigen::VectorXd Bootstrap::BEToVector()
{
	Eigen::VectorXd X = Eigen::VectorXd::Zero(NumConditions);
	int xCount = 0;
	for (int x = 0; x < NumFrag; x++)
	{
		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			X[xCount] = std::get<5>(BEPotential[x][i]);
			xCount++;
		}
	}
	return X;
}

//void Bootstrap::OptMu()
//{
//	// We will do Newton Raphson to optimize the chemical potential.
//	// First, intialize Mu to Chemical Potential and calculate intial loss.
//	double dMu = 1E-4;
//	double Mu = ChemicalPotential;
//	std::vector< std::vector< Eigen::MatrixXd > > Densities = CollectRDM(BEPotential, Mu, State);
//	double Loss = CalcCostChemPot(Densities, BECenterPosition, Input);
//
//	std::cout << "BE: Optimizing chemical potential." << std::endl;
//	// Do Newton's method until convergence
//	while (fabs(Loss) > 1e-6)
//	{
//		// Calculate Loss at Mu + dMu
//		Densities = CollectRDM(BEPotential, Mu + dMu, State);
//		double LossPlus = CalcCostChemPot(Densities, BECenterPosition, Input);
//		double dLdMu = (LossPlus - Loss) / dMu;
//
//		Mu = Mu - Loss / dLdMu;
//
//		// Update Loss at new, updated Mu
//		Densities = CollectRDM(BEPotential, Mu, State);
//		Loss = CalcCostChemPot(Densities, BECenterPosition, Input);
//
//		std::cout << "BE: Iteration yielded mu = " << Mu << " and Loss = " << Loss << std::endl;
//	}
//	std::cout << "BE: Chemical potential converged." << std::endl;
//	// After everything is done, store Mu into ChemicalPotential for the rest of the object.
//	ChemicalPotential = Mu;
//}

void Bootstrap::PrintBEPotential()
{
	std::cout << "BE Site Potential:" << std::endl;
	for (int x = 0; x < NumFrag; x++)
	{
		for (int i = 0; i < BEPotential[x].size(); i++)
		{
			std::cout << x << " " << i << " " << std::get<5>(BEPotential[x][i]) << std::endl;
		}
	}
}

// void Bootstrap::OptMu()
// {
// 	std::cout << "BE-DMET: Optimizing chemical potential." << std::endl;
// 	std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;
// 	for (int x = 0; x < NumFrag; x++)
// 	{
// 		aOneRDMs.push_back(FCIs[x].aOneRDMs[FragState[x]]);
// 		bOneRDMs.push_back(FCIs[x].bOneRDMs[FragState[x]]);
// 	}
// 	// CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aChemicalPotential, bChemicalPotential);
// 	std::vector<double> LMu(2);
// 	LMu = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);

// 	while(fabs(LMu[0]) > 1E-8 || fabs(LMu[1]) > 1E-8)
// 	{
// 		std::vector<Eigen::MatrixXd> aOneRDMsPlusdMu, bOneRDMsPlusdMu;
// 		// std::cout << "Mu = " << aChemicalPotential + dMu << std::endl;
// 		double aMuPlus = aChemicalPotential + dMu;
// 		double bMuPlus = bChemicalPotential + dMu;
// 		CollectRDM(aOneRDMsPlusdMu, bOneRDMsPlusdMu, BEPotential, aMuPlus, bMuPlus);
// 		std::vector<double> LMuPlus = CalcCostChemPot(aOneRDMsPlusdMu, bOneRDMsPlusdMu, aBECenterPosition, bBECenterPosition);
// 		double dLa, dLb;

// 		dLa = (LMuPlus[0] - LMu[0]) / (dMu / 2.0); // I think there's a factor of 2 somewhere, but I don't know why it's here except that it works.
// 		dLb = (LMuPlus[1] - LMu[1]) / (dMu / 2.0);

// 		aChemicalPotential = aChemicalPotential - LMu[0] / dLa;
// 		bChemicalPotential = bChemicalPotential - LMu[1] / dLb;
// 		aOneRDMs.clear();
// 		bOneRDMs.clear();
// 		CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aChemicalPotential, bChemicalPotential);
// 		LMu = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);
// 		std::cout << "BE-DMET: Mu Loss = " << LMu[0] << "\t" << LMu[1] << std::endl;
// 	}
// 	aOneRDMs.clear();
// 	bOneRDMs.clear();
// 	CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aChemicalPotential, bChemicalPotential);
// 	LMu = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);
// 	std::cout << "BE-DMET: Chemical Potential = " << aChemicalPotential << " and " << bChemicalPotential << std::endl;
// }

// Optimization of the chemical potential using Newton's method.
void Bootstrap::OptMu()
{
	std::cout << "BE-DMET: Optimizing chemical potential." << std::endl;
	Eigen::VectorXd MuVec(2);
	MuVec[0] = aChemicalPotential;
	MuVec[1] = bChemicalPotential;
	Eigen::VectorXd L(2);
	Eigen::VectorXd NextL(2);
	Eigen::MatrixXd J = CalcJacobianChemPot(L, MuVec[0], MuVec[1]);
	while (sqrt(L.squaredNorm() / L.size()) > MuTol)
	{
		MuVec = MuVec - J.inverse() * L;
		J = CalcJacobianChemPot(L, MuVec[0], MuVec[1]);
		std::cout << "BE-DMET: Mu Loss = \t" << MuVec[0] << "\t" << L[0] << "\t" << MuVec[1] << "\t" << L[1] << std::endl;
	}
	aChemicalPotential = MuVec[0];
	bChemicalPotential = MuVec[1];
	std::cout << "BE-DMET: Chemical Potential = " << aChemicalPotential << " and " << bChemicalPotential << std::endl;
	UpdateFCIs();
}

void Bootstrap::OptMu(std::vector<double> EndPoints)
{
	std::cout << "BE-DMET: Optimizing chemical potential." << std::endl;
	std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;

	double aX2 = EndPoints[1];
	double bX2 = EndPoints[3];
	double aX1 = EndPoints[0];
	double bX1 = EndPoints[2];

	std::vector<double> L1(2), L2(2);
	CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aX1, bX1);
	L1 = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);
	L2[0] = 1.0; L2[1] = 1.0;

	while (true)
	{
		aOneRDMs.clear();
		bOneRDMs.clear();
		CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aX2, bX2);
		L2 = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);
		std::cout << "BE-DMET: Mu Loss = " << L2[0] << "\t" << L2[1] << std::endl;
		if (fabs(L2[0]) < MuTol && fabs(L2[1]) < MuTol) break;

		double tmpDouble;
		
		if (fabs(L2[0]) > MuTol)
		{
			tmpDouble = aX2;
			aX2 = (aX1 * L2[0] - aX2 * L1[0]) / (L2[0] - L1[0]);
			aX1 = tmpDouble;
		}

		if (fabs(L2[1]) > MuTol)
		{
			tmpDouble = bX2;
			bX2 = (bX1 * L2[1] - bX2 * L1[1]) / (L2[1] - L1[1]);
			bX1 = tmpDouble;
		}

		L1 = L2;
	}

	aChemicalPotential = aX2;
	bChemicalPotential = bX2;
	UpdateFCIs();
	std::cout << "BE-DMET: Chemical Potential = " << aChemicalPotential << " and " << bChemicalPotential << std::endl;
}

std::vector<double> Bootstrap::ScanMu()
{
	std::cout << "BE-DMET: Scanning chemical potential." << std::endl;
	std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;

	std::vector<double> L1(2), L2(2);
	std::vector<double> EndPoints(4);

	int Steps = 200;
	double StepSize = 0.0001;
	double aMu = -0.01;
	double bMu = -0.01;

	bool aDone = false;
	bool bDone = false;

	CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aMu, bMu);
	L1 = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);

	for (int i = 0; i < Steps; i++)
	{
		aOneRDMs.clear();
		bOneRDMs.clear();
		std::vector<Eigen::MatrixXd>().swap(aOneRDMs);
		std::vector<Eigen::MatrixXd>().swap(bOneRDMs);

		CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aMu, bMu);
		L2 = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);

		// if (!aDone && (L1[0] * L2[0] < 0.0))
		// {
		// 	EndPoints[0] = aMu - StepSize;
		// 	EndPoints[1] = aMu;
		// 	aDone = true;
		// }
		// if (!bDone && (L1[1] * L2[1] < 0.0))
		// {
		// 	EndPoints[2] = bMu - StepSize;
		// 	EndPoints[3] = bMu;
		// }

		// if(aDone && bDone)
		// {
		// 	break;
		// }

		std::cout << aMu << "\t" << L2[0] << std::endl;

		aMu += StepSize;
		bMu += StepSize;
	}

	return EndPoints;
}

void Bootstrap::ScanLambda(int n, int FragIndex)
{
	Eigen::VectorXd BEVec = BEToVector();
	Eigen::VectorXd BEVec0 = BEVec;
	
	double Range = 2.0 * dLambda;
	double Steps = 10;
	double StepSize = Range / Steps;
	double Start = BEVec[n] - Range / 2;

	std::cout << "BE-DMET: Beginning Lambda Scan.." << std::endl;

	for (int j = 0; j < Steps + 1; j++)
	{
		BEVec[n] = Start + j * StepSize;
		VectorToBE(BEVec);

		UpdateFCIs();
		std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;
		std::vector< std::vector<double> > aaTwoRDMs, abTwoRDMs, bbTwoRDMs, tmpVecVecDouble;
		for (int x = 0; x < NumFrag; x++)
		{
			aOneRDMs.push_back(FCIs[x].aOneRDMs[FragState[x]]);
			bOneRDMs.push_back(FCIs[x].bOneRDMs[FragState[x]]);
			aaTwoRDMs.push_back(FCIs[x].aaTwoRDMs[FragState[x]]);
			abTwoRDMs.push_back(FCIs[x].abTwoRDMs[FragState[x]]);
			bbTwoRDMs.push_back(FCIs[x].bbTwoRDMs[FragState[x]]);
		}
		std::vector<double> Loss = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, FCIs[FragIndex].aOneRDMs[FragState[FragIndex]], FCIs[FragIndex].bOneRDMs[FragState[FragIndex]], 
		                                          FCIs[FragIndex].aaTwoRDMs[FragState[FragIndex]],  FCIs[FragIndex].abTwoRDMs[FragState[FragIndex]],  FCIs[FragIndex].bbTwoRDMs[FragState[FragIndex]], FragIndex);
		std::cout << Start + j * StepSize << "\t";
		for (int jj = 0; jj < Loss.size(); jj++)
		{
			std::cout << Loss[jj] << "\t";
		}
		std::cout << std::endl;
		if (j == 0 || j == Steps - 1)
		{
			std::cout << "a\n" << FCIs[FragIndex].aOneRDMs[FragState[FragIndex]] << "\nb\n" << FCIs[FragIndex].bOneRDMs[FragState[FragIndex]] << std::endl;
		}
	}
}

void Bootstrap::OptMu_BisectionMethod()
{
	double aMu1 = -0.02;
	double bMu1 = -0.02;
	double aMu2 = 0.02;
	double bMu2 = 0.02;

	if (fabs(aChemicalPotential) > 1E-4 || fabs(bChemicalPotential) > 1E-4)
	{
		aMu1 = aChemicalPotential - 1E-2;
		aMu2 = aChemicalPotential + 1E-2;
		bMu1 = bChemicalPotential - 1E-2;
		bMu2 = bChemicalPotential + 1E-2;
	}

	std::cout << "BE-DMET: Optimizing chemical potential." << std::endl;
	std::vector<Eigen::MatrixXd> aOneRDMs1, bOneRDMs1, aOneRDMs2, bOneRDMs2;

	std::vector<double> L1(2), L2(2);
	
	CollectRDM(aOneRDMs1, bOneRDMs1, BEPotential, aMu1, bMu1);
	L1 = CalcCostChemPot(aOneRDMs1, bOneRDMs1, aBECenterPosition, bBECenterPosition);
	CollectRDM(aOneRDMs2, bOneRDMs2, BEPotential, aMu2, bMu2);
	L2 = CalcCostChemPot(aOneRDMs2, bOneRDMs2, aBECenterPosition, bBECenterPosition);

	// Make sure we are bracketing the root by checcking that both end points have different signs, and stretching the end points until this happens.
	while (L1[0] * L2[0] > 0.0 || L1[1] * L2[1] > 0.0)
	{
		std::cout << "BE-DMET: Bad starting points" << std::endl;
		aMu1 -= 1E-2;
		aMu2 += 1E-2;
		bMu1 -= 1E-2;
		bMu2 += 1E-2;
		aOneRDMs1.clear();
		bOneRDMs1.clear();
		std::vector<Eigen::MatrixXd>().swap(aOneRDMs1);
		std::vector<Eigen::MatrixXd>().swap(bOneRDMs1);
		CollectRDM(aOneRDMs1, bOneRDMs1, BEPotential, aMu1, bMu1);
		L1 = CalcCostChemPot(aOneRDMs1, bOneRDMs1, aBECenterPosition, bBECenterPosition);
		aOneRDMs2.clear();
		bOneRDMs2.clear();
		std::vector<Eigen::MatrixXd>().swap(aOneRDMs2);
		std::vector<Eigen::MatrixXd>().swap(bOneRDMs2);
		CollectRDM(aOneRDMs2, bOneRDMs2, BEPotential, aMu2, bMu2);
		L2 = CalcCostChemPot(aOneRDMs2, bOneRDMs2, aBECenterPosition, bBECenterPosition);
		std::cout << "BE-DMET: Mu Loss =\t" << aMu1 << "\t" << L1[0] << "\t" << bMu1 << "\t" << L1[1] << std::endl;
		std::cout << "BE-DMET: Mu Loss =\t" << aMu2 << "\t" << L2[0] << "\t" << bMu2 << "\t" << L2[1] << std::endl;
	}

	vector<double> LC(2);
	LC[0] = 1.0; LC[1] = 1.0;

	while (fabs(LC[0]) > MuTol || fabs(LC[1]) > MuTol)
	{
		double aMuC = (aMu2 + aMu1) / 2.0;
		double bMuC = (bMu2 + bMu1) / 2.0;

		std::vector<Eigen::MatrixXd> aOneRDMs, bOneRDMs;

		CollectRDM(aOneRDMs, bOneRDMs, BEPotential, aMuC, bMuC);
		LC = CalcCostChemPot(aOneRDMs, bOneRDMs, aBECenterPosition, bBECenterPosition);

		if (LC[0] * L1[0] > 0.0)
		{
			L1[0] = LC[0];
			aMu1 = aMuC;
		}
		else
		{
			L2[0] = LC[0];
			aMu2 = aMuC;
		}

		if ((LC[1] * L1[1]) > 0.0)
		{
			L1[1] = LC[1];
			bMu1 = bMuC;
		}
		else
		{
			L2[1] = LC[1];
			bMu2 = bMuC;
		}

		std::cout << "BE-DMET: Mu Loss =\t" << aMuC << "\t" << LC[0] << "\t" << bMuC << "\t" << LC[1] << std::endl;
		if (fabs(aMu2 - aMu1) < 1E-10 && fabs(bMu2 - bMu1) < 1E-10) // It sometimes happens that we converge but do not get the small loss we want.
		{
			break;
		// 	aMu1 -= 1E-3;
		// 	aMu2 += 1E-3;
		// 	bMu1 -= 1E-3;
		// 	bMu2 += 1E-3;
		// 	// aChemicalPotential = aMuC;
		// 	// bChemicalPotential = bMuC;
		// 	// OptMu();
		// 	// return;
		// 	aOneRDMs1.clear();
		// 	bOneRDMs1.clear();
		// 	aOneRDMs1.shrink_to_fit();
		// 	bOneRDMs1.shrink_to_fit();
		// 	CollectRDM(aOneRDMs1, bOneRDMs1, BEPotential, aMu1, bMu1);
		// 	L1 = CalcCostChemPot(aOneRDMs1, bOneRDMs1, aBECenterPosition, bBECenterPosition);
		// 	aOneRDMs2.clear();
		// 	bOneRDMs2.clear();
		// 	aOneRDMs2.shrink_to_fit();
		// 	bOneRDMs2.shrink_to_fit();
		// 	CollectRDM(aOneRDMs2, bOneRDMs2, BEPotential, aMu2, bMu2);
		// 	L2 = CalcCostChemPot(aOneRDMs2, bOneRDMs2, aBECenterPosition, bBECenterPosition);
		// 	std::cout << "BE-DMET: Mu Loss =\t" << aMu1 << "\t" << L1[0] << "\t" << bMu1 << "\t" << L1[1] << std::endl;
		// 	std::cout << "BE-DMET: Mu Loss =\t" << aMu2 << "\t" << L2[0] << "\t" << bMu2 << "\t" << L2[1] << std::endl;
		}
	}

	aChemicalPotential = (aMu2 + aMu1) / 2.0;
	bChemicalPotential = (bMu2 + bMu1) / 2.0;
	std::cout << "BE-DMET: Chemical Potential = " << aChemicalPotential << " and " << bChemicalPotential << std::endl;
}

void Bootstrap::LineSearch(Eigen::VectorXd& x0, Eigen::VectorXd dx)
{
	double a = 1.0;
	double da = 0.1;
	
	Eigen::VectorXd f0 = Eigen::VectorXd::Zero(NumConditions);
	Eigen::VectorXd fp = Eigen::VectorXd::Zero(NumConditions);
	Eigen::VectorXd fm = Eigen::VectorXd::Zero(NumConditions);

	std::vector<Eigen::MatrixXd> aOneRDMs(NumFrag), bOneRDMs(NumFrag);
	std::vector< std::vector<double> > aaTwoRDMs(NumFrag), abTwoRDMs(NumFrag), bbTwoRDMs(NumFrag);

	double aStep = 1.0;

	std::cout << "BE-DMET: -- Starting Linesearch" << std::endl;

	while(fabs(aStep) > 1E-4)
	{
		int fCount = 0;
		for (int x = 0; x < NumFrag; x++)
		{
			Eigen::VectorXd BEVec0 = x0 + a * dx;
			Eigen::VectorXd BEVecP = x0 + (a + da) * dx;
			Eigen::VectorXd BEVecM = x0 + (a - da) * dx;

			VectorToBE(BEVec0);
			UpdateFCIs();
			for (int xx = 0; xx < NumFrag; xx++)
			{
				aOneRDMs[xx] = FCIs[xx].aOneRDMs[FragState[xx]];
				bOneRDMs[xx] = FCIs[xx].bOneRDMs[FragState[xx]];
				aaTwoRDMs[xx] = FCIs[xx].aaTwoRDMs[FragState[xx]];
				abTwoRDMs[xx] = FCIs[xx].abTwoRDMs[FragState[xx]];
				bbTwoRDMs[xx] = FCIs[xx].bbTwoRDMs[FragState[xx]];
			}
			std::vector<double> Loss0 = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, FCIs[x].aOneRDMs[FragState[x]], FCIs[x].bOneRDMs[FragState[x]], FCIs[x].aaTwoRDMs[FragState[x]], FCIs[x].abTwoRDMs[FragState[x]], FCIs[x].bbTwoRDMs[FragState[x]], x);

			VectorToBE(BEVecP);
			UpdateFCIs();
			for (int xx = 0; xx < NumFrag; xx++)
			{
				aOneRDMs[xx] = FCIs[xx].aOneRDMs[FragState[xx]];
				bOneRDMs[xx] = FCIs[xx].bOneRDMs[FragState[xx]];
				aaTwoRDMs[xx] = FCIs[xx].aaTwoRDMs[FragState[xx]];
				abTwoRDMs[xx] = FCIs[xx].abTwoRDMs[FragState[xx]];
				bbTwoRDMs[xx] = FCIs[xx].bbTwoRDMs[FragState[xx]];
			}			
			std::vector<double> LossP = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, FCIs[x].aOneRDMs[FragState[x]], FCIs[x].bOneRDMs[FragState[x]], FCIs[x].aaTwoRDMs[FragState[x]], FCIs[x].abTwoRDMs[FragState[x]], FCIs[x].bbTwoRDMs[FragState[x]], x);

			VectorToBE(BEVecM);
			UpdateFCIs();
			for (int xx = 0; xx < NumFrag; xx++)
			{
				aOneRDMs[xx] = FCIs[xx].aOneRDMs[FragState[xx]];
				bOneRDMs[xx] = FCIs[xx].bOneRDMs[FragState[xx]];
				aaTwoRDMs[xx] = FCIs[xx].aaTwoRDMs[FragState[xx]];
				abTwoRDMs[xx] = FCIs[xx].abTwoRDMs[FragState[xx]];
				bbTwoRDMs[xx] = FCIs[xx].bbTwoRDMs[FragState[xx]];
			}			
			std::vector<double> LossM = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, FCIs[x].aOneRDMs[FragState[x]], FCIs[x].bOneRDMs[FragState[x]], FCIs[x].aaTwoRDMs[FragState[x]], FCIs[x].abTwoRDMs[FragState[x]], FCIs[x].bbTwoRDMs[FragState[x]], x);
			
			for (int i = 0; i < Loss0.size(); i++)
			{
				f0[fCount] = Loss0[i];
				fp[fCount] = LossP[i];
				fm[fCount] = LossM[i];
				fCount++;
			}
		}
		double L0 = sqrt(f0.squaredNorm() / f0.size());
		double LP = sqrt(fp.squaredNorm() / fp.size());
		double LM = sqrt(fm.squaredNorm() / fm.size());

		double d1L = (LP - LM) / (2.0 * da);
		double d2L = (LP - 2.0 * L0 + LM) / (da * da);

		a = a - d1L / d2L;
		aStep = fabs(d1L / d2L);

		std::cout << "BE-DMET: a = " << a << " and Lambda Loss = " << L0 << std::endl; 
	}

	// Update FCIs when done.
	x0 = x0 + a * dx;
	VectorToBE(x0);
	UpdateFCIs();
}

double Bootstrap::LineSearchCoarse(Eigen::VectorXd& x0, Eigen::VectorXd dx)
{
	
	Eigen::VectorXd f0 = Eigen::VectorXd::Zero(NumConditions);
	Eigen::VectorXd fp = Eigen::VectorXd::Zero(NumConditions);
	Eigen::VectorXd fm = Eigen::VectorXd::Zero(NumConditions);

	std::vector<Eigen::MatrixXd> aOneRDMs(NumFrag), bOneRDMs(NumFrag);
	std::vector< std::vector<double> > aaTwoRDMs(NumFrag), abTwoRDMs(NumFrag), bbTwoRDMs(NumFrag);

	// Hard code the test multiplicative factors.
	std::vector<double> TestFactors{2., 1., 0.1, 0.01}; //{2.000, 1.000, 0.100, 0.010};
	std::vector<double> Losses; // Holds the loss from each test factor so we can pick the smallest

	std::cout << "BE-DMET: -- Starting Linesearch" << std::endl;

	for (int j = 0; j < TestFactors.size(); j++)
	{
		Eigen::VectorXd BEVec0 = x0 + TestFactors[j] * dx;

		VectorToBE(BEVec0);
		UpdateFCIs();

		for (int xx = 0; xx < NumFrag; xx++)
		{
			aOneRDMs[xx] = FCIs[xx].aOneRDMs[FragState[xx]];
			bOneRDMs[xx] = FCIs[xx].bOneRDMs[FragState[xx]];
			aaTwoRDMs[xx] = FCIs[xx].aaTwoRDMs[FragState[xx]];
			abTwoRDMs[xx] = FCIs[xx].abTwoRDMs[FragState[xx]];
			bbTwoRDMs[xx] = FCIs[xx].bbTwoRDMs[FragState[xx]];
		}

		int fCount = 0;
		for (int x = 0; x < NumFrag; x++)
		{
			std::vector<double> Loss0 = CalcCostLambda(aOneRDMs, bOneRDMs, aaTwoRDMs, abTwoRDMs, bbTwoRDMs, FCIs[x].aOneRDMs[FragState[x]], FCIs[x].bOneRDMs[FragState[x]], FCIs[x].aaTwoRDMs[FragState[x]], FCIs[x].abTwoRDMs[FragState[x]], FCIs[x].bbTwoRDMs[FragState[x]], x);
			for (int i = 0; i < Loss0.size(); i++)
			{
				f0[fCount] = Loss0[i];
				fCount++;
			}
		}
		double L0 = sqrt(f0.squaredNorm() / f0.size());
		Losses.push_back(L0);
		std::cout << "BE-DMET: a = " << TestFactors[j] << " and Lambda Loss = " << L0 << std::endl; 
	}

	// Find the smallest loss and choose that factor.
	double a = TestFactors[0];
	double SmallestLoss = Losses[0];
	for (int j = 1; j < TestFactors.size(); j++)
	{
		if (Losses[j] < SmallestLoss)
		{
			a = TestFactors[j];
			SmallestLoss = Losses[j];
		}
	}

	return a;
}

void Bootstrap::OptLambda()
{
	Eigen::VectorXd x = BEToVector();
	Eigen::VectorXd f;
	Eigen::MatrixXd J = CalcJacobian(f);

	std::cout << "BE-DMET: Optimizing site potential" << std::endl;
	if (sqrt(f.squaredNorm() / f.size()) < LambdaTol) // If the chemical potential optimization did not cause a need to change, we still need to update the FCIs with it.
	{
		UpdateFCIs();
	}
	while (sqrt(f.squaredNorm() / f.size()) > LambdaTol)
	{
		// x = x - J.inverse() * f;
		// VectorToBE(x); // Updates the BEPotential for the J and f update next.
		// UpdateFCIs(); // Inputs potentials into the FCI that varies.
		Eigen::VectorXd dx = -J.inverse() * f;
		double a = 1.0;
		if (doLineSearch) a = LineSearchCoarse(x, dx);
		x = x + a * dx;
		VectorToBE(x);
		UpdateFCIs();

		J = CalcJacobian(f); // Update here to check the loss.
		// J = 0.1 * J; // Hardcoded "linesearch"
		std::cout << "BE-DMET: Lambda Loss = " << sqrt(f.squaredNorm() / f.size()) << std::endl;
	}
	std::cout << "BE-DMET: Site potential obtained\n" << x << "\nBE-DMET: with loss \n" << sqrt(f.squaredNorm() / f.size()) << std::endl;
}

void Bootstrap::NewtonRaphson()
{
	std::cout << "BE-DMET: Beginning initialization for site potential optimization." << std::endl;
	*Output << "BE-DMET: Beginning initialization for site potential optimization." << std::endl;

	std::cout << "BE-DMET: Optimizing site potential." << std::endl;
	*Output << "BE-DMET: Optimizing site potential." << std::endl;

	int NRIteration = 1;
	std::vector<double> LMu(2);
	LMu[0] = 1.0;
	LMu[1] = 1.0;

	// Eigen::VectorXd f;

	while (sqrt((LMu[0] * LMu[0] + LMu[1] * LMu[1]) / 2.0) > MuTol)
	// do
	{
		std::cout << "BE-DMET: -- Running Newton-Raphson iteration " << NRIteration << "." << std::endl;
		*Output << "BE-DMET: -- Running Newton-Raphson iteration " << NRIteration << "." << std::endl; 
		OptMu();
		// OptMu_BisectionMethod();
		OptLambda();

		// Eigen::MatrixXd J;
		// J = CalcJacobian(f);
		LMu = CalcCostChemPot();
		std::cout << "BE-DMET: -- -- Newton-Raphson error = " << sqrt((LMu[0] * LMu[0] + LMu[1] * LMu[1]) / 2.0) << std::endl;
		*Output << "BE-DMET: -- -- Newton-Raphson error = " << sqrt((LMu[0] * LMu[0] + LMu[1] * LMu[1]) / 2.0) << std::endl;
		double BEEnergy = CalcBEEnergy();
		std::cout << "BE-DMET: -- -- BE Energy for iteration " << NRIteration << " is " << BEEnergy << std::endl;
		*Output << "BE-DMET: -- -- BE Energy for iteration " << NRIteration << " is " << BEEnergy << std::endl;
		NRIteration++;
	}
	// while (fabs(f.sum()) > 1E-8);
}

void Bootstrap::doBootstrap(InputObj &Input, std::vector<Eigen::MatrixXd> &MFDensity, std::ofstream &Output)
{
	FragmentOrbitals = Input.FragmentOrbitals;
	EnvironmentOrbitals = Input.EnvironmentOrbitals;
	NumAO = Input.NumAO;
	NumOcc = Input.NumOcc;
	NumFrag = Input.NumFragments;

	// This will hold all of our impurity densities, calculated from using the BE potential.
	std::vector< Eigen::MatrixXd > ImpurityDensities(NumFrag);
	std::vector< Eigen::Tensor<double, 4> > Impurity2RDM(NumFrag);
	std::vector< Eigen::VectorXd > ImpurityEigenstates(NumFrag);

	CollectSchmidt(MFDensity, Output); // Runs a function to collect all rotational matrices in corresponding to each fragment.

	// Now that we have the rotation matrices, we can iterate through each fragment and get the impurity densities.
	for (int x = 0; x < NumFrag; x++)
	{
		// ImpurityFCI(ImpurityDensities[x], Input, x, RotationMatrices[x], ChemicalPotential, State, Impurity2RDM[x], ImpurityEigenstates[x]);
	}

	// Now we iterate through each unique BE potential element and solve for the Lambda potential in each case.
	// In each case, we create a BENewton object that handles the Newton Raphson optimization.
}

void Bootstrap::doBootstrap(InputObj &Inp, std::vector<Eigen::MatrixXd> &aMFDensity, std::vector<Eigen::MatrixXd> &bMFDensity, std::ofstream &Output)
{
	FragmentOrbitals = Inp.FragmentOrbitals;
	EnvironmentOrbitals = Inp.EnvironmentOrbitals;
	NumAO = Inp.NumAO;
	NumOcc = Inp.NumOcc;
	// NumFrag = Inp.NumFragments;
	FragState = Inp.ImpurityStates;
	BathState = Inp.BathStates;
	BEInputName = Inp.BEInput;
	Input = Inp;

	aFragPos.clear();
	bFragPos.clear();
	aBathPos.clear();
	bBathPos.clear();

	CollectSchmidt(aMFDensity, bMFDensity, Output); // Runs a function to collect all rotational matrices in corresponding to each fragment.

	/* If the system is translationally symmetric, we commence by redefining the number of fragments and matching conditions. Specifically, we will
	   reduce the number of fragments to one and redefine the matching condition in the scope of that one fragment. Then we will multiply the energy
	   at the end by the appropriate amount. */
	if (isTS)
	{
		TrueNumFrag = NumFrag;
		NumFrag = 1;
		
		NumFragCond.clear(); std::vector<int>().swap(NumFragCond);
		NumFragCond.push_back(BEPotential[0].size());
		NumConditions = BEPotential[0].size();

		std::vector< std::tuple< int, int, int, int, int, double, bool, bool > > BEPotentialTS = BEPotential[0];
		BEPotential.clear(); std::vector< std::vector< std::tuple< int, int, int, int, int, double, bool, bool > > >().swap(BEPotential);
		BEPotential.push_back(BEPotentialTS);

		auto BECenterPosition0 = aBECenterPosition[0];
		aBECenterPosition.clear(); std::vector< std::vector< int > >().swap(aBECenterPosition);
		aBECenterPosition.push_back(BECenterPosition0);

		BECenterPosition0 = bBECenterPosition[0];
		bBECenterPosition.clear(); std::vector< std::vector< int > >().swap(bBECenterPosition);
		bBECenterPosition.push_back(BECenterPosition0);
	}

	FCIs.clear();
	// Generate impurity FCI objects for each impurity.
	for (int x = 0; x < TrueNumFrag; x++)
	{
		std::vector<int> xaFragPos, xaBathPos, xbFragPos, xbBathPos;
		GetCASPos(Input, x, xaFragPos, xaBathPos, true);
		GetCASPos(Input, x, xbFragPos, xbBathPos, false);
		aFragPos.push_back(xaFragPos);
		bFragPos.push_back(xbFragPos);
		aBathPos.push_back(xaBathPos);
		bBathPos.push_back(xbBathPos);
	}

	for (int x = 0; x < NumFrag; x++)
	{
		std::vector<int> aActiveList, aVirtualList, aCoreList, bActiveList, bVirtualList, bCoreList;
        GetCASList(Input, x, aActiveList, aCoreList, aVirtualList, true);
        GetCASList(Input, x, bActiveList, bCoreList, bVirtualList, false);

		FCI xFCI(Input, Input.FragmentOrbitals[x].size(), Input.FragmentOrbitals[x].size(), aCoreList, aActiveList, aVirtualList, bCoreList, bActiveList, bVirtualList);
		xFCI.ERIMapToArray(Input.Integrals, aRotationMatrices[x], bRotationMatrices[x], aActiveList, bActiveList);
		if (doDavidson) xFCI.runFCI();
		else xFCI.DirectFCI();
		xFCI.getSpecificRDM(FragState[x], true);
		// if (x == 1 || x == 5)
		// {
		// 	std::cout << "xFCI " << x << std::endl;
		// 	xFCI.PrintERI(true);
		// }
		FCIs.push_back(xFCI);
	}

	aBECenterIndex.clear();
	bBECenterIndex.clear();
	for (int x = 0; x < aBECenterPosition.size(); x++)
	{
		std::vector<int> xaBECenterIndex;
		for (int i = 0; i < aBECenterPosition[x].size(); i++)
		{
			int CenterIdx = OrbitalToReducedIndex(aBECenterPosition[x][i], x, true);
			xaBECenterIndex.push_back(CenterIdx);
		}
		aBECenterIndex.push_back(xaBECenterIndex);
	}
	for (int x = 0; x < bBECenterPosition.size(); x++)
	{
		std::vector<int> xbBECenterIndex;
		for (int i = 0; i < bBECenterPosition[x].size(); i++)
		{
			int CenterIdx = OrbitalToReducedIndex(bBECenterPosition[x][i], x, false);
			xbBECenterIndex.push_back(CenterIdx);
		}
		bBECenterIndex.push_back(xbBECenterIndex);
	}

	for (int x = 0; x < FCIs.size(); x++)
	{
		FCI xFCI(FCIs[x]);
		FCIsBase.push_back(xFCI);
	}
	// BEEnergy = CalcBEEnergy();
	// std::cout << "BE-DMET: DMET Energy = " << BEEnergy << std::endl;
	// return;

	// ScanMu();
	// OptMu_BisectionMethod();
	OptMu();
	
	// aChemicalPotential = 0.0014133736; bChemicalPotential = 0.0014133736;
	// Eigen::VectorXd x(24);
	// // x << -0.0023840008,-0.0023171942,-0.0023840011,-0.0023171963,-0.0023171948,-0.0023840010,-0.0023840008,-0.0023171976,-0.0023171959,-0.0023840006,-0.0023840009,-0.0023171974,-0.0023171959,-0.0023840005,-0.0023840009,-0.0023171980,-0.0023171953,-0.0023840005,-0.0023840009,-0.0023171980,-0.0023171955,-0.0023840004,-0.0023171969,-0.0023840013;
	// x << -0.0004988821,-0.0014229979,-0.0004985315,-0.0014212909,-0.0014230472,-0.0004988732,-0.0004985309,-0.0014213258,-0.0014230368,-0.0004988654,-0.0004985473,-0.0014213229,-0.0014229710,-0.0004988727,-0.0004985547,-0.0014212512,-0.0014229484,-0.0004988830,-0.0004985439,-0.0014212074,-0.0014229747,-0.0004988907,-0.0014212326,-0.0004985321;
	// VectorToBE(x);
	double OneShotE = CalcBEEnergy();
	std::cout << "BE-DMET: BE0 Energy = " << OneShotE << std::endl;
	Output << "BE0 Energy = " << OneShotE << std::endl;
	// return;
	NewtonRaphson();
	UpdateFCIs();
	BEEnergy = CalcBEEnergy();
	std::cout << "BE-DMET: Fragment Alpha 1RDMs:\n";
	Output << "BE-DMET: Fragment Alpha 1RDMs:\n";
	for (int x = 0; x < NumFrag; x++)
	{
		std::cout << FCIs[x].aOneRDMs[FragState[x]] << std::endl;
		Output << FCIs[x].aOneRDMs[FragState[x]] << std::endl;
	}
	std::cout << "BE-DMET: Fragment Beta 1RDMs:\n";
	Output << "BE-DMET: Fragment Beta 1RDMs:\n";
	for (int x = 0; x < NumFrag; x++)
	{
		std::cout << FCIs[x].bOneRDMs[FragState[x]] << std::endl;
		Output << FCIs[x].bOneRDMs[FragState[x]] << std::endl;
	}
	std::cout << "BE-DMET: DMET Energy = " << BEEnergy << std::endl;
	Output << "DMET Energy = " << BEEnergy << std::endl;
}

void Bootstrap::printDebug(std::ofstream &Output)
{
	for (int x = 0; x < RotationMatrices.size(); x++)
	{
		Output << RotationMatrices[x] << std::endl;
		Output << "\n";
	}
}

// This calculates the BE energy assuming chemical potential and BE potential have both been optimized.
double Bootstrap::CalcBEEnergy()
{
	double Energy = 0;
	// Loop through each fragment to calculate the energy of each.
	std::cout << "BE-DMET: Calculating DMET Energy..." << std::endl;
	*Output << "BE-DMET: Calculating DMET Energy..." << std::endl;
	double FragEnergy;

	for (int x = 0; x < NumFrag; x++)
	{
		std::vector<Eigen::MatrixXd> OneRDM;
		// std::vector<int> aBECenterIndex, bBECenterIndex;
		// for (int i = 0; i < aBECenterPosition[x].size(); i++)
		// {
		// 	int idx = OrbitalToReducedIndex(aBECenterPosition[x][i], x, true);
		// 	aBECenterIndex.push_back(idx);
		// }
		// for (int i = 0; i < bBECenterPosition[x].size(); i++)
		// {
		// 	int idx = OrbitalToReducedIndex(bBECenterPosition[x][i], x, false);
		// 	bBECenterIndex.push_back(idx);
		// }
		FragEnergy = FCIsBase[x].calcImpurityEnergy(FragState[x], aBECenterIndex[x], bBECenterIndex[x], FCIs[x].aOneRDMs[FragState[x]], FCIs[x].bOneRDMs[FragState[x]], FCIs[x].aaTwoRDMs[FragState[x]], FCIs[x].abTwoRDMs[FragState[x]], FCIs[x].bbTwoRDMs[FragState[x]]);
		Energy += FragEnergy;
		std::cout << "BE-DMET: -- Energy of Fragment " << x << " is " << FragEnergy << std::endl;
		*Output << "BE-DMET: -- Energy of Fragment " << x << " is " << FragEnergy << std::endl;
		std::cout << "BE-DMET: -- Alpha Density of Fragment " << x << " is\n" << FCIs[x].aOneRDMs[FragState[x]] << std::endl;
		std::cout << "BE-DMET: --  Beta Density of Fragment " << x << " is\n" << FCIs[x].bOneRDMs[FragState[x]] << std::endl;
		*Output << "BE-DMET: -- Alpha Density of Fragment " << x << " is\n" << FCIs[x].aOneRDMs[FragState[x]] << std::endl;
		*Output << "BE-DMET: --  Beta Density of Fragment " << x << " is\n" << FCIs[x].bOneRDMs[FragState[x]] << std::endl;
		std::cout << "FCI Energy = " << FCIs[x].Energies[FragState[x]] << std::endl;
	}
	if (isTS) Energy *= TrueNumFrag;
	Energy += Input.Integrals["0 0 0 0"];
	return Energy;
}

//double Bootstrap::CalcBEEnergyByFrag()
//{
//	std::cout << "BE-DMET: Calculating one shot BE energy..." << std::endl;
//	*Output << "BE-DMET: Calculating one shot BE energy..." << std::endl;
//	std::vector<double> FragEnergies;
//	// Collect fragment energies for each type of state. This depends on St, which impurity state is chosen and which bath state is used for the embedding.
//	for (int St = 0; St < MaxState; St++)
//	{
//		std::vector<Eigen::MatrixXd> OneRDM;
//		std::vector<double> FragEnergy;
//		FragEnergy = BEImpurityFCI(OneRDM, Input, 0, RotationMatrices[0], ChemicalPotential, St, BEPotential[0], MaxState, BECenterPosition[0]);
//		FragEnergies.push_back(FragEnergy[0]);
//	}
//	double Energy = 0;
//	for (int x = 0; x < NumFrag; x++)
//	{
//		Energy += FragEnergies[FragState[x]];
//		std::cout << "BE-DMET: -- Energy of Fragment " << x << " is " << FragEnergies[FragState[x]] << std::endl;
//		*Output << "BE-DMET: -- Energy of Fragment " << x << " is " << FragEnergies[FragState[x]] << std::endl;
//	}
//	Energy += Input.Integrals["0 0 0 0"];
//	return Energy;
//}

void Bootstrap::runDebug()
{
	std::cout << "BE-DMET: Beginning Bootstrap Embedding..." << std::endl;
	*Output << "BE-DMET: Beginning Bootstrap Embedding..." << std::endl;
	NewtonRaphson(); // Comment out to do one shot
	double Energy = CalcBEEnergy();
	// double Energy = CalcBEEnergyByFrag();
	std::cout << "BE-DMET: DMET Energy = " << Energy << std::endl;
	*Output << "BE-DMET: DMET Energy = " << Energy << std::endl;
}
