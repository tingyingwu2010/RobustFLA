//
// Created by chenghaowang on 24/07/17.
//
#include "../include/Solution.h"

void ApproximateFLA(Network *pNetwork, const vdList &vdParameter, double dEpsilon, double dCoefPi, double *dSumBenefits,
                    int *iMaxNbConflict, int iModeMethod, int iFeasibleSize, bool modeGenerateSigma, int iModeRandom) {
//    int iNbFlightNotAssigned = 0;
    int iNbFlightChangeLevel = 0;
    IloEnv env;
    std::ofstream cplexLogFile("cplexLog.txt", std::ios::out | std::ios::app);

    // Initialize the coefficient Pi for all flight in the network.
    pNetwork->InitCoefPi(dCoefPi);
    // Initialize the feasible flight levels for all flights in the network.
    pNetwork->InitFeasibleList(iFeasibleSize);
    // Initialize  the flight levels list of the network.
    pNetwork->InitFlightLevelsList();
    // Initialize the sigma for the random departure time.
    if (iModeMethod > 1) {
        pNetwork->SetSigma(vdParameter, iModeRandom, modeGenerateSigma);
    }

    FlightVector vpFlightList = pNetwork->getFlightsList();
    LevelVector viLevelsList = pNetwork->getFlightLevelsList();

    std::cout << "[INFO] Starting ApproximateFLA method..." << std::endl;
    ProcessClock processClock;
    //Start the process clock.
    processClock.start();
    try {

        // Try to assign the flights.
        SolveFLA(vpFlightList, env, vdParameter, viLevelsList, processClock, dEpsilon, dCoefPi,
                 dSumBenefits, iMaxNbConflict, iModeMethod,
                 iModeRandom, cplexLogFile);
        // Get the number of flights that change its most preferred flight level.
        iNbFlightChangeLevel = getNbFlightsChangeLevel(vpFlightList);
    } catch (IloException &e) { std::cerr << e.getMessage() << std::endl; }
    catch (...) { std::cerr << "error" << std::endl; }
    cplexLogFile.close();
    env.end();
    processClock.end();
    std::cout << "Nb of flights change level: " << iNbFlightChangeLevel << std::endl;
    std::cout << "Max conflict: " << *iMaxNbConflict << std::endl;
    std::cout << "Sum benefits: " << *dSumBenefits << std::endl;
    std::cout << "Elapsed time: " << processClock.getCpuTime() << std::endl;
}

int getNbFlightsChangeLevel(FlightVector &vpFlightList) {
    int iNbFlightChangeLevel = 0;
    for (auto &&fi : vpFlightList) {
        if (fi->getCurrentLevel() != fi->getDefaultLevel()) {
            iNbFlightChangeLevel++;
        }
    }
    return iNbFlightChangeLevel;
}

bool
SolvingFLAByLevelP(FlightVector &vpFlightList, FlightsLevelMap &infeasibleFlightMap,
                   const IloEnv &env, const vdList &vdParameter, LevelExamine &levelEx,
                   FlightLevelAssignmentMap &flightLevelAssignmentMap, double epsilon, int *iMaxNbConflict,
                   Level iProcessingLevel, int iModeMethod, int iModeRandom, std::ofstream &cplexLogFile) {
    //Process the assignment problem for each flight level
    std::cout << "[INFO] Level: " << iProcessingLevel << std::endl;
    FlightVector CandidateFlightList;
    FlightVector ConflictFlightList;
    vdList Mi, Pi;
    double dDelayTime = 0;
    bool bWait = true;

    //Initialize the candidate flight list in current processing flight level.
    for (auto &&flight: vpFlightList) {
        Level iPreferredLevel = flight->getDefaultLevel();
        if (iPreferredLevel == iProcessingLevel) {
            CandidateFlightList.push_back(flight);
        }
    }

    //Remove infeasible flights
    for (auto &&flight:infeasibleFlightMap[iProcessingLevel]) {
        auto position = std::find(CandidateFlightList.begin(), CandidateFlightList.end(), flight);
        if (position != CandidateFlightList.end()) {
            CandidateFlightList.erase(position);
            std::cout << "\tRemove: " << flight->getCode() << std::endl;
        }
    }

    //Initialize the conflicted flight list.
    for (auto &&fi: CandidateFlightList) {
        for (auto &&fj: CandidateFlightList) {
            if (*fi == *fj) {
                continue;
            }
            double prob = fi->CalculateProbabilityConflictAndDelayForFlight(fj, &dDelayTime, &bWait,
                                                                            iModeMethod < 3);
            if (prob > 0) {
                ConflictFlightList.push_back(fi);
                break;
            }
        }
    }

    levelEx[iProcessingLevel] = true;
    std::cout << "\tCandidateFlights: " << CandidateFlightList.size() << std::endl;
    std::cout << "\tConflictFlights: " << ConflictFlightList.size() << std::endl;

    //Assign all conflict-free flight.
    for (auto &&flight: CandidateFlightList) {
        if (!contains(ConflictFlightList, flight)) {
            flight->setCurrentLevel(iProcessingLevel);
            flightLevelAssignmentMap[flight].first = true;
            flightLevelAssignmentMap[flight].second = iProcessingLevel;
        }
    }

    //If the conflict flight list size equal 0, then go to process the next flight level.
    int iConflictFlightsSize = (int) ConflictFlightList.size();
    if (iConflictFlightsSize == 0) {
        std::cout << "\tNo conflict flights" << std::endl;
        return true;
    }

    double **ppdConflictProbability = CreateTable(iConflictFlightsSize);
    double **ppdDelayTime = CreateTable(iConflictFlightsSize);
    InitTable(ppdConflictProbability, iConflictFlightsSize);
    InitTable(ppdDelayTime, iConflictFlightsSize);
    CalculateConflictProbability(ConflictFlightList, ppdConflictProbability, ppdDelayTime, iModeMethod < 3);

    //Calculate Mi list and maximal conflict count.
    for (int i = 0; i < iConflictFlightsSize; i++) {
        double sumPenalCost = 0.0;
        int iConflictCount = 0;
        for (int j = 0; j < iConflictFlightsSize; j++) {
            if (ppdConflictProbability[i][j] > 0) {
                sumPenalCost += std::max(0.0, ppdDelayTime[i][j]);
                iConflictCount++;
            }
        }
        Mi.push_back(sumPenalCost);
        if (iConflictCount > *iMaxNbConflict) {
            *iMaxNbConflict = iConflictCount;
        }
    }
    //Calculate Pi list.
    for (auto &&fi: ConflictFlightList) {
        double time = (fi->getArrivingTime() - fi->getDepartureTime());
        Pi.push_back(std::max(60.0, time) * fi->getCoefPi());
    }

    viList viSearchList;
    viList viConstraintList;
    for (int i = 0; i < iConflictFlightsSize; i++) {
        viSearchList.push_back(i);
    }
    //Relax the most infeasible constraint.
    int iMinIndexArgs = MinIndexArgs0(ConflictFlightList, viSearchList, Pi, ppdConflictProbability, ppdDelayTime,
                                      iModeMethod < 3);
    if (iMinIndexArgs == -1) {
        DestroyTable(ppdConflictProbability, iConflictFlightsSize);
        DestroyTable(ppdDelayTime, iConflictFlightsSize);
        return true;
    }
    std::cout << "\t\t\tStart" << std::endl;
    std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
    viConstraintList.push_back(iMinIndexArgs);
    viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs), viSearchList.end());
    Solver *solver = new Solver(env, cplexLogFile);
    solver->initDecisionVariables(iConflictFlightsSize);
    solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
    solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime, iConflictFlightsSize);
    solver->solve();
    IloNumArray decisionVariablesValues = solver->getDecisionVariablesValues();
    delete solver;
    int iMinIndexArgsFromFeaCheck = -1;
    bool feasible = false;
    //Test de feasibility of each constraint.
    //Relax the most infeasible constraint, until the solutionC has a high expected confidence.
    switch (iModeMethod) {
        case 0:/*enumeration method*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityEnumeration(viSearchList, Pi, decisionVariablesValues, ppdConflictProbability,
                                                  ppdDelayTime,
                                                  epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, true);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 1:/*Hoeffding*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityHoeffding(viSearchList, Pi, decisionVariablesValues,
                                                ppdConflictProbability,
                                                ppdDelayTime,
                                                epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, true);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 2:/*MonteCarlo*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityMonteCarlo(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                 vdParameter,
                                                 epsilon, &iMinIndexArgsFromFeaCheck, iModeRandom, true);
                if (iMinIndexArgsFromFeaCheck == -1) {
                    iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                  ppdConflictProbability,
                                                  ppdDelayTime, true);
                    if (iMinIndexArgs == -1) {
                        break;
                    }
                } else {
                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 3:/*Gaussian*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityGaussian(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                               ppdConflictProbability, ppdDelayTime,
                                               epsilon, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, false);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 4:/*MonteCarlo with departure time*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityMonteCarlo(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                 vdParameter,
                                                 epsilon, &iMinIndexArgsFromFeaCheck, iModeRandom, false);
                if (iMinIndexArgsFromFeaCheck == -1) {
                    iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                  ppdConflictProbability,
                                                  ppdDelayTime, false);
                    if (iMinIndexArgs == -1) {
                        break;
                    }
                } else {
                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 5:/*enumeration method with departure time*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityEnumeration(viSearchList, Pi, decisionVariablesValues, ppdConflictProbability,
                                                  ppdDelayTime,
                                                  epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, false);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        default:
            std::cerr << "Not support ! " << std::endl;
            abort();
            break;
    }

    DestroyTable(ppdConflictProbability, iConflictFlightsSize);
    DestroyTable(ppdDelayTime, iConflictFlightsSize);
    if (!feasible && viSearchList.size() == 0) {
        std::cout << "\tNo feasible solutionS found!" << std::endl;
        if (ConflictFlightList.size() < 3) {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
        } else if (ConflictFlightList.size() < 5) {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[1]]);
        } else {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[1]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[2]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[3]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[4]]);
        }
        return false;
    } else {
        for (int i = 0; i < iConflictFlightsSize; i++) {
            if (decisionVariablesValues[i] == 1) {
                Flight *fi = ConflictFlightList[i];
                fi->setCurrentLevel(iProcessingLevel);
                flightLevelAssignmentMap[fi].first = true;
                flightLevelAssignmentMap[fi].second = iProcessingLevel;
            }
        }
        return true;
    }
}

bool
SolvingFLAByLevelPP(FlightVector &vpFlightList, FlightsLevelMap &infeasibleFlightMap,
                    FlightVector &vpPreviousFlightList,
                    const IloEnv &env, const vdList &vdParameter, LevelExamine &levelEx,
                    FlightLevelAssignmentMap &flightLevelAssignmentMap, double epsilon,
                    int *iMaxNbConflict,
                    Level iProcessingLevel, int iModeMethod, int iModeRandom, std::ofstream &cplexLogFile) {
    //Process the assignment problem for each flight level
    std::cout << "[INFO] Level: " << iProcessingLevel << std::endl;
    FlightVector CandidateFlightList;
    FlightVector ConflictFlightList;
    vdList Mi, Pi;
    double dDelayTime = 0;
    bool bWait = true;

    //Initialize the candidate flight list in current processing flight level.
    for (auto &&flight: vpFlightList) {
        const LevelVector &levels = flight->getFeasibleLevelList();
        Level iPreferredLevel = flight->getDefaultLevel();

        if (!flightLevelAssignmentMap[flight].first) {
            if (iPreferredLevel == iProcessingLevel) {
                CandidateFlightList.push_back(flight);
            } else {
                auto position = std::find(levels.begin(), levels.end(), iProcessingLevel);
                if (position != levels.end()) {
                    bool valid = true;
                    auto iter = levels.begin();
                    while (valid && iter < position) {
                        valid = levelEx[*iter];
                        iter++;
                    }
                    if (valid) {
                        CandidateFlightList.push_back(flight);
                    }
                }
//                if(contains(levels, iProcessingLevel) && levelEx[iProcessingLevel]){
//                    CandidateFlightList.push_back(flight);
//                }
            }
        } else {
            if (flightLevelAssignmentMap[flight].second == iProcessingLevel) {
                CandidateFlightList.push_back(flight);
            }
        }
    }

    //Remove infeasible flights
    for (auto &&flight:infeasibleFlightMap[iProcessingLevel]) {
        auto position = std::find(CandidateFlightList.begin(), CandidateFlightList.end(), flight);
        if (position != CandidateFlightList.end()) {
            CandidateFlightList.erase(position);
            std::cout << "\tRemove: " << flight->getCode() << std::endl;
        }
    }

    //Initialize the conflicted flight list.
    for (auto &&fi: CandidateFlightList) {
        for (auto &&fj: CandidateFlightList) {
            if (*fi == *fj) {
                continue;
            }
            double prob = fi->CalculateProbabilityConflictAndDelayForFlight(fj, &dDelayTime, &bWait,
                                                                            iModeMethod < 3);
            if (prob > 0) {
                ConflictFlightList.push_back(fi);
                break;
            }
        }
    }

    levelEx[iProcessingLevel] = true;
    std::cout << "\tCandidateFlights: " << CandidateFlightList.size() << std::endl;
    std::cout << "\tConflictFlights: " << ConflictFlightList.size() << std::endl;

    for (auto &&flight:ConflictFlightList) {
        if (!contains(vpPreviousFlightList, flight)) {
            bool conflict = false;
            for (auto &&fj : ConflictFlightList) {
                if (*flight == *fj) {
                    continue;
                }
                double prob = flight->CalculateProbabilityConflictAndDelayForFlight(fj, &dDelayTime, &bWait,
                                                                                    iModeMethod < 3);
                if (prob > 0) {
//                    flightLevelAssignmentMap[fj].first = false;
//                    flightLevelAssignmentMap[fj].second = -1;
                    conflict = true;
                }
            }
            if (conflict) {
                flightLevelAssignmentMap[flight].first = false;
                flightLevelAssignmentMap[flight].second = -1;
            }
        }
    }

    vpPreviousFlightList.clear();
    for (auto &&flight:ConflictFlightList) {
        vpPreviousFlightList.push_back(flight);
    }

    //Assign all conflict-free flight.
    for (auto &&flight: CandidateFlightList) {
        if (!contains(ConflictFlightList, flight)) {
            flight->setCurrentLevel(iProcessingLevel);
            flightLevelAssignmentMap[flight].first = true;
            flightLevelAssignmentMap[flight].second = iProcessingLevel;
        }
    }

    //If the conflict flight list size equal 0, then go to process the next flight level.
    int iConflictFlightsSize = (int) ConflictFlightList.size();
    if (iConflictFlightsSize == 0) {
        std::cout << "\tNo conflict flights" << std::endl;
        return true;
    }

    double **ppdConflictProbability = CreateTable(iConflictFlightsSize);
    double **ppdDelayTime = CreateTable(iConflictFlightsSize);
    InitTable(ppdConflictProbability, iConflictFlightsSize);
    InitTable(ppdDelayTime, iConflictFlightsSize);
    CalculateConflictProbability(ConflictFlightList, ppdConflictProbability, ppdDelayTime, iModeMethod < 3);

    //Calculate Mi list and maximal conflict count.
    for (int i = 0; i < iConflictFlightsSize; i++) {
        double sumPenalCost = 0.0;
        int iConflictCount = 0;
        for (int j = 0; j < iConflictFlightsSize; j++) {
            if (i != j && ppdConflictProbability[i][j] > 0) {
                sumPenalCost += std::max(0.0, ppdDelayTime[i][j]);
                iConflictCount++;
            }
        }
        Mi.push_back(sumPenalCost);
        if (iConflictCount > *iMaxNbConflict) {
            *iMaxNbConflict = iConflictCount;
        }
    }
    //Calculate Pi list.
    for (auto &&fi: ConflictFlightList) {
        double time = (fi->getArrivingTime() - fi->getDepartureTime());
        Pi.push_back(std::max(60.0, time) * fi->getCoefPi());
    }

    viList viSearchList;
    viList viConstraintList;
    for (int i = 0; i < iConflictFlightsSize; i++) {
        viSearchList.push_back(i);
    }
    //Relax the most infeasible constraint.
    int iMinIndexArgs = MinIndexArgs0(ConflictFlightList, viSearchList, Pi, ppdConflictProbability, ppdDelayTime,
                                      iModeMethod < 3);
    if (iMinIndexArgs == -1) {
        DestroyTable(ppdConflictProbability, iConflictFlightsSize);
        DestroyTable(ppdDelayTime, iConflictFlightsSize);
        return true;
    }
    std::cout << "\t\t\tStart" << std::endl;
    std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
    viConstraintList.push_back(iMinIndexArgs);
    viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs), viSearchList.end());
    Solver *solver = new Solver(env, cplexLogFile);
    solver->initDecisionVariables(iConflictFlightsSize);
    solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
    solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime, iConflictFlightsSize);
    solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
    solver->solve();
    IloNumArray decisionVariablesValues = solver->getDecisionVariablesValues();
    delete solver;
    int iMinIndexArgsFromFeaCheck = -1;
    bool feasible = false;
    //Test de feasibility of each constraint.
    //Relax the most infeasible constraint, until the solutionC has a high expected confidence.
    switch (iModeMethod) {
        case 0:/*enumeration method*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityEnumeration(viSearchList, Pi, decisionVariablesValues, ppdConflictProbability,
                                                  ppdDelayTime,
                                                  epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, true);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 1:/*Hoeffding*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityHoeffding(viSearchList, Pi, decisionVariablesValues,
                                                ppdConflictProbability,
                                                ppdDelayTime,
                                                epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, true);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 2:/*MonteCarlo*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityMonteCarlo(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                 vdParameter,
                                                 epsilon, &iMinIndexArgsFromFeaCheck, iModeRandom, true);
                if (iMinIndexArgsFromFeaCheck == -1) {
                    if (viSearchList.size() == 1) {
                        iMinIndexArgs = viSearchList[0];
                    } else {
                        iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                      ppdConflictProbability,
                                                      ppdDelayTime, true);
                        if (iMinIndexArgs == -1) {
                            break;
                        }
                    }
                } else {
                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 3:/*Gaussian*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityGaussian(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                               ppdConflictProbability, ppdDelayTime,
                                               epsilon, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, false);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 4:/*MonteCarlo with departure time*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityMonteCarlo(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                 vdParameter,
                                                 epsilon, &iMinIndexArgsFromFeaCheck, iModeRandom, false);
                if (iMinIndexArgsFromFeaCheck == -1) {
                    iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                                  ppdConflictProbability,
                                                  ppdDelayTime, false);
                    if (iMinIndexArgs == -1) {
                        break;
                    }
                } else {
                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        case 5:/*enumeration method with departure time*/
            while (!feasible &&
                   viSearchList.size() > 0) {
                feasible = FeasibilityEnumeration(viSearchList, Pi, decisionVariablesValues, ppdConflictProbability,
                                                  ppdDelayTime,
                                                  epsilon, iConflictFlightsSize, &iMinIndexArgsFromFeaCheck);
//                if (iMinIndexArgsFromFeaCheck == -1) {
                iMinIndexArgs = MinIndexArgs1(ConflictFlightList, viSearchList, Pi, decisionVariablesValues,
                                              ppdConflictProbability,
                                              ppdDelayTime, false);
                if (iMinIndexArgs == -1) {
                    break;
                }
//                } else {
//                    iMinIndexArgs = iMinIndexArgsFromFeaCheck;
//                }
                std::cout << "\t\t[INFO] Index: " << iMinIndexArgs << std::endl;
                viConstraintList.push_back(iMinIndexArgs);
                //Remove the most infeasible constraint from search list.
                viSearchList.erase(std::remove(viSearchList.begin(), viSearchList.end(), iMinIndexArgs),
                                   viSearchList.end());
                //ReInitialize the model, then resolve it.
                solver = new Solver(env, cplexLogFile);
                solver->initDecisionVariables(iConflictFlightsSize);
                solver->initFunctionObjective(ConflictFlightList, iProcessingLevel);
                solver->initConstraint(viConstraintList, Mi, Pi, ppdConflictProbability, ppdDelayTime,
                                       iConflictFlightsSize);
                solver->prefixAssignedFlight(ConflictFlightList, flightLevelAssignmentMap, iProcessingLevel);
                solver->solve();
                decisionVariablesValues = solver->getDecisionVariablesValues();
                delete solver;
            }
            break;
        default:
            std::cerr << "Not support ! " << std::endl;
            abort();
            break;
    }

    DestroyTable(ppdConflictProbability, iConflictFlightsSize);
    DestroyTable(ppdDelayTime, iConflictFlightsSize);
    if (!feasible && viSearchList.size() == 0) {
        std::cout << "\tNo feasible solutionS found!" << std::endl;
        if (ConflictFlightList.size() < 3) {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
        } else if (ConflictFlightList.size() < 5) {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[1]]);
        } else {
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[0]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[1]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[2]]);
            infeasibleFlightMap[iProcessingLevel].push_back(ConflictFlightList[viConstraintList[3]]);
        }
        return false;
    } else {
        for (int i = 0; i < iConflictFlightsSize; i++) {
            if (decisionVariablesValues[i] == 1) {
                Flight *fi = ConflictFlightList[i];
                fi->setCurrentLevel(iProcessingLevel);
                flightLevelAssignmentMap[fi].first = true;
                flightLevelAssignmentMap[fi].second = iProcessingLevel;
            }
        }
        return true;
    }
}

void SolveFLA(FlightVector &vpFlightList, const IloEnv &env, const vdList &vdParameter,
              LevelVector &viLevelsList, ProcessClock &processClock, double epsilon, double dCoefPi,
              double *dSumBenefits, int *iMaxNbConflict,
              int iModeMethod, int iModeRandom, std::ofstream &cplexLogFile) {
    LevelExamine levelEx;
    FlightLevelAssignmentMap flightLevelAssignmentMap;
    FlightsLevelMap infeasibleFlightMap;
//    FlightsLevelMap conflictFreeList;
    LevelQueue queueLevel;
    FlightVector vpPreviousFlightList;
    *dSumBenefits = 0;
    *iMaxNbConflict = 0;

    //Initialize the flight assignment map.
    for (auto &&flight: vpFlightList) {
        flightLevelAssignmentMap[flight] = std::make_pair(false, -1);
    }

    //Initialize the examine flight levels list.
    for (auto &&level: viLevelsList) {
        levelEx[level] = false;
    }

    //Initialize current flight level for each flight as its most preferred one.
    for (auto &&fi: vpFlightList) {
        fi->resetLevel();
    }

    //Initialize the infeasible flight level map
    for (auto &&level: viLevelsList) {
        FlightVector infeasibleFlight;
//        FlightVector conflictFreeFlight;
        infeasibleFlightMap.insert(std::make_pair(level, infeasibleFlight));
//        conflictFreeMap.insert(std::make_pair(level, conflictFreeFlight));
    }

    //Initialize the flight level queue.
    for (auto &&processingLevel : viLevelsList) {
        queueLevel.push_back(processingLevel);
    }

//    //Process the problem P
//    while (!queueLevel.empty()) {
//        Level processingLevel = queueLevel.front();
//        queueLevel.pop_front();
//        bool solved = false;
//        while (!solved) {
//            solved = SolvingFLAByLevelP(vpFlightList, infeasibleFlightMap, env, vdParameter, levelEx,
//                                        flightLevelAssignmentMap, epsilon, iMaxNbConflict, processingLevel, iModeMethod,
//                                        iModeRandom,
//                                        cplexLogFile);
//        }
//    }
//    FlightVector vpFlightNotAssigned;
//    for (auto &&fi: flightLevelAssignmentMap) {
//        if (!fi.second.first) {
//            vpFlightNotAssigned.push_back(fi.first);
//        }
//    }
    // Get the number of flights that change its most preferred flight level.
//    int iNbFlightChangeLevel = getNbFlightsChangeLevel(vpFlightList);
    // Get the number of unassigned flights.
//    int iNbFlightNotAssigned = (int) vpFlightNotAssigned.size();
//    std::cout << "Solve the problem P:" << std::endl;
//    std::cout << "\tNb of flights change level: " << iNbFlightChangeLevel << std::endl;
//    std::cout << "\tNb of flights not be assigned: " << iNbFlightNotAssigned << std::endl;
//    processClock.end();
//    std::cout << "\tElapsed time: " << processClock.getCpuTime() << std::endl;
    int iNbFlightNotAssigned = 1;
    int iNbFlightChangeLevel;
    while (iNbFlightNotAssigned > 0) {
//        //Initialize the examine flight levels list.
//        for (auto &&level: viLevelsList) {
//            levelEx[level] = false;
//        }
//
//        //Initialize current flight level for each flight as its most preferred one.
//        for (auto &&fi: vpFlightList) {
//            fi->resetLevel();
//        }
        //Initialize the flight level queue.
        for (auto &&processingLevel : viLevelsList) {
            queueLevel.push_back(processingLevel);
        }
        while (!queueLevel.empty()) {
            Level processingLevel = queueLevel.front();
            queueLevel.pop_front();
            bool solved = false;
            while (!solved) {
                solved = SolvingFLAByLevelPP(vpFlightList, infeasibleFlightMap, vpPreviousFlightList,
                                             env, vdParameter,
                                             levelEx, flightLevelAssignmentMap, epsilon, iMaxNbConflict,
                                             processingLevel, iModeMethod,
                                             iModeRandom,
                                             cplexLogFile);
            }
        }
        FlightVector flightNotAssigned;
        for (auto &&fi: flightLevelAssignmentMap) {
            if (!fi.second.first) {
                flightNotAssigned.push_back(fi.first);
                fi.first->setCoefPi(fi.first->getCoefPi() + 0.05);
            }
        }
        // Get the number of flights that change its most preferred flight level.
        iNbFlightChangeLevel = getNbFlightsChangeLevel(vpFlightList);
        // Get the number of unassigned flights.
        if ((int) flightNotAssigned.size() > iNbFlightNotAssigned - 1) {
            for (auto &&fi : flightNotAssigned) {
                Level newLevel = findNextFeasibleLevel(fi->getDefaultLevel(), fi->getLastFeasibleLevel());
                if (newLevel != -1) {
                    fi->addNewFeasibleLevel(newLevel);
                    if (!contains(viLevelsList, newLevel)) {
                        viLevelsList.push_back(newLevel);
                    }
//                    fi->setCoefPi(dCoefPi);
                }
            }
        }
        iNbFlightNotAssigned = (int) flightNotAssigned.size();
        std::cout << "Solve the problem P':" << std::endl;
        std::cout << "\tNb of flights change level: " << iNbFlightChangeLevel << std::endl;
        std::cout << "\tNb of flights not be assigned: " << iNbFlightNotAssigned << std::endl;
        processClock.end();
        std::cout << "\tElapsed time: " << processClock.getCpuTime() << std::endl;
    }
    for (auto &&fi: flightLevelAssignmentMap) {
        auto index = std::find(fi.first->getFeasibleLevelList().begin(), fi.first->getFeasibleLevelList().end(),
                               fi.second.second);
        if (index != fi.first->getFeasibleLevelList().end()) {
            long lIndex = index - fi.first->getFeasibleLevelList().begin();
            *dSumBenefits += exp(3 - lIndex);
        }
    }
}

bool FeasibilityMonteCarlo(FlightVector &vpConflictedFlightList, const viList &viSearchList, const vdList &vdPi,
                           const IloNumArray &decisionVariables, const vdList &vdParameter, double dEpsilon,
                           int *piIndex, int iModeRandom, bool bGeoMethod) {
    int iConflictedFlightsSize = (int) vpConflictedFlightList.size();
    int iConflictedCounter = 0;
    int iMaxConflict = -1;
    *piIndex = -1;
    double **ppdConflictProbability = CreateTable(iConflictedFlightsSize);
    double **ppdDelayTime = CreateTable(iConflictedFlightsSize);
    int *table = new int[iConflictedFlightsSize];
    for (int i = 0; i < iConflictedFlightsSize; i++) {
        table[i] = 0;
    }
    //Simulate 1000 scenarios
    for (int a = 0; a < 1000; a++) {
        InitTable(ppdDelayTime, iConflictedFlightsSize);
        InitTable(ppdConflictProbability, iConflictedFlightsSize);
        bool test = true;
        //Generate a new scenario from current flight list.
        for (auto fj : vpConflictedFlightList) {
            Time iOldDepartureTime = fj->getDepartureTime();
            Time iDelta = 0;
            switch (iModeRandom) {
                case 0:
                    iDelta = GaussianDistribution1(vdParameter, fj->getSigma());
                    break;
                case 1:
                    iDelta = GaussianDistribution2(vdParameter, fj->getSigma());
                    break;
                default:
                    iDelta = GaussianDistribution3(vdParameter, fj->getSigma());
                    break;
            }
            fj->GenerateNewFlight(iOldDepartureTime + iDelta, bGeoMethod);
        }
        CalculateConflictProbability(vpConflictedFlightList, ppdConflictProbability, ppdDelayTime, bGeoMethod);
        for (int i = 0; i < iConflictedFlightsSize; i++) {
            if (decisionVariables[i] == 1) {
                double sum = 0;
                for (int j = 0; j < iConflictedFlightsSize; j++) {
                    if (ppdDelayTime[i][j] > 0 && decisionVariables[j] == 1) {
                        sum += std::max(0.0, ppdDelayTime[i][j]);
                    }
                }
                if (sum > vdPi[i]) {
                    test = false;
                    table[i] = table[i] + 1;
                    break;
                }
            }
        }
        if (!test) {
            iConflictedCounter++;
        }
        for (auto &&fj: vpConflictedFlightList) {
            fj->resetRouteTimeList();
        }
    }
    for (auto &&item : viSearchList) {
        if (decisionVariables[item] == 1 && table[item] > iMaxConflict) {
            iMaxConflict = table[item];
            *piIndex = item;
        }
    }
    delete table;
    DestroyTable(ppdDelayTime, iConflictedFlightsSize);
    DestroyTable(ppdConflictProbability, iConflictedFlightsSize);
    if (dEpsilon == 0.05) {
        if (1000 - iConflictedCounter < 961) {
            return false;
        }
    }
    if (dEpsilon == 0.1) {
        if (1000 - iConflictedCounter < 915) {
            return false;
        }
    }
    if (dEpsilon == 0.15) {
        if (1000 - iConflictedCounter < 868) {
            return false;
        }
    }
    if (dEpsilon == 0.2) {
        if (1000 - iConflictedCounter < 821) {
            return false;
        }
    }
    if (dEpsilon == 0.25) {
        if (1000 - iConflictedCounter < 772) {
            return false;
        }
    }
    std::cout << "\t\t\tFeasibility ==> ok!" << std::endl;
    return true;
}

bool FeasibilityGaussian(FlightVector &vpConflictedFlightList, const viList &viSearchList, const vdList &vdPi,
                         const IloNumArray &decisionVariables, double **ppdConflictProbability, double **ppdDelayTime,
                         double dEpsilon, int *piIndex) {
    int iSize = (int) vpConflictedFlightList.size();
    *piIndex = -1;
    for (int i = 0; i < iSize; i++) {
        if (decisionVariables[i] == 1) {
            double exp_mu = 0.0;
            double exp_sigma_2 = 0.0;
            int iCounter = 0;
            double dLeftConstrainedFeasibility = 1;
            double mu;
            double sigma_2;
            for (int j = 0; j < iSize; j++) {
                if (ppdConflictProbability[i][j] > 0 && decisionVariables[j] == 1) {
                    mu = ppdDelayTime[i][j];
                    sigma_2 = (pow(vpConflictedFlightList[i]->getSigma(), 2) +
                               pow(vpConflictedFlightList[j]->getSigma(), 2));
                    dLeftConstrainedFeasibility *= 0.5 * (boost::math::erf((vdPi[i] - mu) / (sqrt(2 * sigma_2))) + 1);
                    exp_mu += mu;
                    exp_sigma_2 += sigma_2;
                    iCounter++;
                }
            }
            double dFeasibility = 0.5 * (boost::math::erf((vdPi[i] - exp_mu) / (sqrt(2 * exp_sigma_2))) + 1);
            if (iCounter > 1) {
                dFeasibility *= dLeftConstrainedFeasibility;
            }
            std::cout << "\t\t\ti: " << i << "==>Feas: " << dFeasibility << "===> Eps: " << 1 - dEpsilon << std::endl;

            if (dFeasibility < 1.0 - dEpsilon) {
                return false;
            }
        }
    }
    std::cout << "\t\t\tFeasibility ==> ok!" << std::endl;
    return true;
}

bool FeasibilityHoeffding(const viList &viSearchList, const vdList &vdPi,
                          const IloNumArray &decisionVariables,
                          double **ppdConflictProbability, double **ppdDelayTime, double dEpsilon,
                          int iConflictedFlightSize, int *piIndex) {
    *piIndex = -1;
    for (int i = 0; i < iConflictedFlightSize; i++) {
        double dInFeasibility = 0;
        double temp2 = 0;
        if (decisionVariables[i] == 1) {
            for (int j = 0; j < iConflictedFlightSize; j++) {
                if (ppdConflictProbability[i][j] > 0) {
                    double maxPenalCost = std::max(0.0, ppdDelayTime[i][j]) * decisionVariables[j];
                    dInFeasibility += ppdConflictProbability[i][j] * maxPenalCost / 2;
                    temp2 += pow(maxPenalCost, 2);
                }
            }
            dInFeasibility = exp(-(2 * pow(vdPi[i] - dInFeasibility, 2)) / temp2);
            std::cout << "\t\t\ti: " << i << "==>Feas: " << 1 - dInFeasibility << "===> Eps: " << 1 - dEpsilon
                      << std::endl;
            if (dInFeasibility > dEpsilon) {
                return false;
            }
        }
    }
    std::cout << "\t\t\tFeasibility ==> ok!" << std::endl;
    return true;
}

bool FeasibilityEnumeration(const viList &viSearchList, const vdList &vdPi, const IloNumArray &decisionVariables,
                            double **ppdConflictProbability, double **ppdDelayTime, double dEpsilon,
                            int iConflictedFlightsSize, int *piIndexMostInfeasible) {
    *piIndexMostInfeasible = -1;
    //Iterate all conflict flights in current flight level
    for (int i = 0; i < iConflictedFlightsSize; i++) {
        //For each assigned flight(x[i] = 1) in current level of last solutionC
        if (decisionVariables[i] == 1) {
            int iConflictFlightsOfI = 0;
            viList viCandidateConflictFlightOfI;
            double dInFeasibility = 0;
            double dSumPenalCost = 0;
            for (int j = 0; j < iConflictedFlightsSize; j++) {
                if (ppdConflictProbability[i][j] > 0) {
                    dSumPenalCost += std::max(0.0, ppdDelayTime[i][j]) * decisionVariables[j];
                    viCandidateConflictFlightOfI.push_back(j);
                    iConflictFlightsOfI++;
                }
            }
            bool bNotFinish = true;
            int k = iConflictFlightsOfI;
            //If the constraint is infeasible, then test all its combinations
            // util all the combination with k items will be feasible.
            if (dSumPenalCost > vdPi[i]) {
                while (bNotFinish && k > 0) {
                    qviList qviCombinationList = Combination(viCandidateConflictFlightOfI, k);
                    bool bValid = false;
                    //For each case of combination, test the occurrence probability.
                    for (auto subCombination: qviCombinationList) {
                        double dOccurrenceProbability = 1;
                        double dSumPenalCostP = 0.0;
                        viList viComplementList = getComplement((int) viCandidateConflictFlightOfI.size(),
                                                                subCombination);
                        for (auto indexJ: subCombination) {
                            int j = viCandidateConflictFlightOfI[indexJ];
                            dOccurrenceProbability *= ppdConflictProbability[i][j];
                            dSumPenalCostP += std::max(0.0, ppdDelayTime[i][j]);
                        }
                        for (auto indexJ: viComplementList) {
                            int j = viCandidateConflictFlightOfI[indexJ];
                            dOccurrenceProbability *= 1 - ppdConflictProbability[i][j];
                        }
                        if (dSumPenalCostP > vdPi[i]) {
                            bValid = true;
                            dInFeasibility += dOccurrenceProbability;
                        }
                    }
                    k--;
                    bNotFinish = bValid;
                }
                std::cout << "\t\t\ti: " << i << "==>Feas: " << 1 - dInFeasibility << "===> Eps: " << 1 - dEpsilon
                          << std::endl;
                if (dInFeasibility > dEpsilon) {
                    return false;
                }
            }
        }
    }
    std::cout << "\t\t\tFeasibility ==> ok!" << std::endl;
    return true;
}

int MinIndexArgs1(FlightVector &vpConflictFlightList, const viList &viSearchList, const vdList &vdPi,
                  const IloNumArray &decisionVariables, double **ppdConflictProbability, double **ppdDelayTime,
                  bool bMethod) {
    int iMinIndex = -1;
    int iConflictedFlightSize = (int) vpConflictFlightList.size();
    double dMaxValue = std::numeric_limits<double>::max();
    if (bMethod) {
        for (auto &&index: viSearchList) {
            double dSumPenalCost = 0;
            for (int j = 0; j < iConflictedFlightSize; j++) {
                if (ppdConflictProbability[index][j] > 0) {
                    dSumPenalCost += std::max(0.0, ppdDelayTime[index][j]) * ppdConflictProbability[index][j] *
                                     decisionVariables[j] / 2;
                }
            }
            //i : min Pi-E[sum{p_ij}]
            dSumPenalCost = vdPi[index] - dSumPenalCost;
            if (dSumPenalCost < dMaxValue) {
                dMaxValue = dSumPenalCost;
                iMinIndex = index;
            }
        }
    } else {
        double dMinFeasibility = std::numeric_limits<double>::max();
        for (auto &&index: viSearchList) {
            double exp_mu = 0.0;
            double exp_sigma_2 = 0.0;
            int iCounter = 0;
            double dLeftConstrainedFeasibility = 1;
            double mu;
            double sigma_2;
            for (int j = 0; j < iConflictedFlightSize; j++) {
                if (ppdConflictProbability[index][j] > 0 && decisionVariables[j] == 1) {
                    mu = ppdDelayTime[index][j];
                    sigma_2 = (pow(vpConflictFlightList[index]->getSigma(), 2) +
                               pow(vpConflictFlightList[j]->getSigma(), 2));
                    dLeftConstrainedFeasibility *=
                            0.5 * (boost::math::erf((vdPi[index] - mu) / (sqrt(2 * sigma_2))) + 1);
                    exp_mu += mu;
                    exp_sigma_2 += sigma_2;
                    iCounter++;
                }
            }
            double dFeasibility = 0.5 * (boost::math::erf((vdPi[index] - exp_mu) / (sqrt(2 * exp_sigma_2))) + 1);
            if (iCounter > 1) {
                dFeasibility *= dLeftConstrainedFeasibility;
            }
            if (dFeasibility < dMinFeasibility) {
                dMinFeasibility = dFeasibility;
                iMinIndex = index;
            }
        }
    }
//    if (iMinIndex == -1) {
//        std::cerr << "[ERROR] the selected index invalid !" << std::endl;
//    }
    return iMinIndex;
}

int MinIndexArgs0(FlightVector &vpConflictFlightList, const viList &viSearchList, const vdList &vdPi,
                  double **ppdConflictProbability, double **ppdDelayTime, bool bGeoMethod) {
    int iMinIndex = -1;
    int iConflictedFlightSize = (int) vpConflictFlightList.size();
    double dMaxValue = std::numeric_limits<double>::max();
    if (bGeoMethod) {
        for (auto &&index: viSearchList) {
            double dSumPenalCost = 0;
            for (int j = 0; j < iConflictedFlightSize; j++) {
                if (ppdConflictProbability[index][j] > 0) {
                    dSumPenalCost += std::max(0.0, ppdDelayTime[index][j]) * ppdConflictProbability[index][j] / 2;
                }
            }
            //i : min Pi-E[sum{p_ij}]
            dSumPenalCost = vdPi[index] - dSumPenalCost;
            if (dSumPenalCost < dMaxValue) {
                dMaxValue = dSumPenalCost;
                iMinIndex = index;
            }
        }
    } else {
        double dMinFeasibility = std::numeric_limits<double>::max();
        for (auto &&index: viSearchList) {
            double exp_mu = 0.0;
            double exp_sigma_2 = 0.0;
            int iCounter = 0;
            double dLeftConstrainedFeasibility = 1;
            double mu;
            double sigma_2;
            for (int j = 0; j < iConflictedFlightSize; j++) {
                if (ppdConflictProbability[index][j] > 0) {
                    mu = ppdDelayTime[index][j];
                    sigma_2 = pow(vpConflictFlightList[index]->getSigma(), 2) +
                              pow(vpConflictFlightList[j]->getSigma(), 2);
                    dLeftConstrainedFeasibility *=
                            0.5 * (boost::math::erf((vdPi[index] - mu) / (sqrt(2 * sigma_2))) + 1);
                    exp_mu += mu;
                    exp_sigma_2 += sigma_2;
                    iCounter++;
                }
            }
            double dFeasibility = 0.5 * (boost::math::erf((vdPi[index] - exp_mu) / (sqrt(2 * exp_sigma_2))) + 1);
            if (iCounter > 1) {
                dFeasibility *= dLeftConstrainedFeasibility;
            }
            if (dFeasibility < dMinFeasibility) {
                dMinFeasibility = dFeasibility;
                iMinIndex = index;
            }
        }
    }

//    if (iMinIndex == -1) {
//        std::cerr << "[ERROR] the selected index invalid !" << std::endl;
//    }
    return iMinIndex;
}

void CalculateConflictProbability(FlightVector &vpConflictFlightList, double **ppdConflictProbability,
                                  double **ppdDelayTime, bool bGeometricMethod) {
    int iSize = (int) vpConflictFlightList.size();
    bool bWait = true;
    double dDelay = 0;
    for (int i = 0; i < iSize - 1; i++) {
        Flight *fi = vpConflictFlightList[i];
        for (int j = i + 1; j < iSize; j++) {
            Flight *fj = vpConflictFlightList[j];
            ppdConflictProbability[i][j] = ppdConflictProbability[j][i] = fi->CalculateProbabilityConflictAndDelayForFlight(
                    fj, &dDelay,
                    &bWait, bGeometricMethod);
            if (bWait) {
                ppdDelayTime[i][j] = dDelay;
                ppdDelayTime[j][i] = 0;
            } else {
                ppdDelayTime[i][j] = 0;
                ppdDelayTime[j][i] = dDelay;
            }
        }
    }
}

void DestroyTable(double **ppdTable, int iSize) {
    for (int i = 0; i < iSize; i++) {
        delete[] ppdTable[i];
    }
    delete[] ppdTable;
}

void InitTable(double **ppdTable, int iSize) {
    for (int i = 0; i < iSize; i++) {
        for (int j = 0; j < iSize; j++) {
            ppdTable[i][j] = 0;
        }
    }
}

double **CreateTable(int iSize) {
    double **ppdDouble = new double *[iSize];
    for (int i = 0; i < iSize; i++) {
        ppdDouble[i] = new double[iSize];
    }
    return ppdDouble;
}

double GaussianDistribution3(const vdList &vdParameter, double dSigma) {
    std::default_random_engine generator;

    uni_dist UniformDist(0, 1);
    double p = UniformDist(generator);
    if (p < vdParameter[5]) {
        normal_dist NormalDist(vdParameter[0] * vdParameter[3],
                               dSigma);
        return -fabs(NormalDist(generator));
    } else {
        normal_dist NormalDist(vdParameter[1] * vdParameter[4],
                               dSigma);
        return fabs(NormalDist(generator));
    }
}

double GaussianDistribution2(const vdList &vdParameter, double dSigma) {
    std::default_random_engine generator;
    normal_dist NormalDist(0, dSigma);
    return NormalDist(generator);
}

double GaussianDistribution1(const vdList &vdParameter, double dSigma) {
    std::default_random_engine generator;
    normal_dist NormalDist((vdParameter[1] - vdParameter[0]) / 2,
                           dSigma);
    return NormalDist(generator);
}

viList getComplement(int iSize, viList &candidateList) {
    viList resultList;
    for (int i = 0; i < iSize; i++) {
        auto index = std::find(candidateList.begin(), candidateList.end(), i);
        if (index == candidateList.end()) {
            resultList.push_back(i);
        }
    }
    return resultList;
}

qviList Combination(const viList &viConstraintList, int k) {
    qviList qviCombinationList;
    //Initialize the deque with one item.
    for (int i = 0; i < (int) viConstraintList.size() - k + 1; i++) {
        viList candidateList;
        candidateList.push_back(i);
        qviCombinationList.push_back(candidateList);
    }
    //Do a combination for each item in deque until all items have a size of k.
    while ((int) qviCombinationList.front().size() < k) {
        int iValue = qviCombinationList.front()[qviCombinationList.front().size() - 1];
        for (int i = iValue + 1;
             i < (int) viConstraintList.size() - k + 1 + (int) qviCombinationList.front().size(); i++) {
            viList candidateList = viList((viList &&) qviCombinationList.front());
            candidateList.push_back(i);
            qviCombinationList.push_back(candidateList);
        }
        qviCombinationList.pop_front();
    }
    return qviCombinationList;
}
