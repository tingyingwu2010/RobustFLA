//
// Created by chenghaowang on 06/07/17.
//

#include "../include/Solver.h"

void Solver::initDecisionVariables(int iSize) {
    IloNumVarArray x(env, iSize, 0, 1, ILOBOOL);
    decisionVariables = x;
}

void Solver::initFunctionObjective(FlightVector &ConflictedFlightList, int iProcessingLevel) {
    IloExpr obj(env);
    for (int i = 0; i < (int) ConflictedFlightList.size(); i++) {
        Flight *pFlight = ConflictedFlightList[i];
        auto index = std::find(pFlight->getFeasibleLevelList().begin(), pFlight->getFeasibleLevelList().end(),
                               iProcessingLevel);
        if (index != pFlight->getFeasibleLevelList().end()) {
            long lIndex = index - pFlight->getFeasibleLevelList().begin();
            obj += decisionVariables[i] * exp(3 - lIndex);
        } else {
            std::cerr << std::endl << "[Fatal Error]: the flight is processing out of it feasible flight level list !"
                      << std::endl;
            abort();
        }
    }
    functionObjective = obj;
    model.add(IloMaximize(env, functionObjective));
}

IloNumArray Solver::getDecisionVariablesValues() const {
    return decisionVariablesValues;
}

double Solver::getFunctionObjectiveValue() const {
    return dFunctionObjectiveValue;
}


void Solver::solve() {
    IloNumArray values(env);
    if (solver.solve()) {
        solver.getValues(values, decisionVariables);
        decisionVariablesValues = values;
        dFunctionObjectiveValue = solver.getObjValue();
    } else {
        std::cerr << "[ERROR] Solved Failed!" << std::endl;
        abort();
    }
}

Solver::~Solver() {
    functionObjective.end();
    decisionVariables.end();
    model.end();
    solver.end();
}

Solver::Solver(IloEnv env) {
    Solver::env = env;
    IloModel model_temp(env);
    Solver::model = model_temp;
    IloCplex solver_temp(model);
    Solver::solver = solver_temp;
    env.setOut(env.getNullStream());
    solver.setOut(env.getNullStream());
    solver.setParam(IloCplex::Threads, 2);
}

Solver::Solver(IloEnv env, std::ofstream &log) {
    Solver::env = env;
    IloModel model_temp(env);
    Solver::model = model_temp;
    IloCplex solver_temp(model);
    Solver::solver = solver_temp;
    env.setOut(log);
    solver.setOut(log);
    solver.setParam(IloCplex::Threads, 2);
}

void Solver::initConstraint(const viList &constraintList, const vdList &Mi, const vdList &Pi, double **ppdDelayTime,
                            int iNbConflictedFlights) {
    for (auto &&i: constraintList) {
        IloExpr constraint(env);
        for (int j = 0; j < iNbConflictedFlights; j++) {
            if (i == j) {
                constraint += Mi[j] * decisionVariables[j];
            } else {
                constraint += std::max(0.0, ppdDelayTime[i][j]) * decisionVariables[j];
            }
        }
        IloConstraint c(constraint <= Mi[i] + Pi[i]);
        model.add(c);
    }
}

void
Solver::prefixAssignedFlight(FlightVector &ConflictedFlightList, FlightLevelAssignmentMap &flightLevelAssignmentMap,
                             Level iProcessingLevel) {
    for (int i = 0; i < (int) ConflictedFlightList.size(); i++) {
        if (flightLevelAssignmentMap.at(ConflictedFlightList[i]).second == iProcessingLevel &&
            flightLevelAssignmentMap.at(ConflictedFlightList[i]).first) {
            model.add(decisionVariables[i] == 1);
        }
    }
}


