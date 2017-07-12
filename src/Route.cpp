//
// Created by chenghaowang on 23/05/17.
//

#include "../include/Route.h"
#include "../include/Input.h"

Route *Route::GenerateNewRoute(Time iNewDepartureTime) {
    std::default_random_engine generator;
    Route *newRoute = new Route(getDefaultLevel(), getPointAtI(0)->clone(iNewDepartureTime));
    for (int i = 1; i < getPointListSize(); i++) {
        double velocity = getVelocityFromPoint(i - 1);
        normal_dist NormalDistribution(0, 1.0 / 300);
        uni_dist UniformDistribution(0.0, 1.0);
        double dRandom = UniformDistribution(generator);
        velocity = velocity * ((dRandom > 0.5) ? 1 + fabs(NormalDistribution(generator)) : 1 - fabs(NormalDistribution(
                generator)));
        double dDistance = distanceBetween(getPointAtI(i)->getPosition(), getPointAtI(i - 1)->getPosition());
        Time dDeltaT = (Time) (dDistance / velocity);
        newRoute->addNewPoint(getPointAtI(i)->clone(newRoute->getArrivingTimeAtPoint(i - 1) + dDeltaT));
    }
    return newRoute;
}

Route *Route::GenerateNewRoute2(Time iNewDepartureTime) {
    Route *newRoute = new Route(getDefaultLevel(), getPointAtI(0)->clone(iNewDepartureTime));
    Time dDeltaT = iNewDepartureTime - getArrivingTimeAtPoint(0);
    for (int i = 1; i < getPointListSize(); i++) {
        newRoute->addNewPoint(getPointAtI(i)->clone(newRoute->getArrivingTimeAtPoint(i - 1) + dDeltaT));
    }
    return newRoute;
}

double Route::getVelocityFromPoint(int iIndexPoint) {
    if (iIndexPoint == 0) {
        return -1;
    }
    double dDeltaT = getArrivingTimeAtPoint(iIndexPoint) - getArrivingTimeAtPoint(iIndexPoint - 1);
    double dDistance = distanceBetween(getPositionAtPoint(iIndexPoint), getPositionAtPoint(iIndexPoint-1));
    if (dDeltaT != 0) {
        return dDistance / dDeltaT;
    }
    std::cerr << "[Fatal Error] the arriving time between two consecutive points is 0." << std::endl;
    return -1;
}

bool Route::selfCheck() {
    bool bValid = true;
    for(int i =1; i < getPointListSize(); i++){
        if (getArrivingTimeAtPoint(i) <= getArrivingTimeAtPoint(i-1))
            bValid = false;
    }
    return bValid;
}

double Route::probabilityAndDelay(double dT1, double dT2, double dV1, double dV2, double dCosA, double *pdDelayTime,
                                  bool bFlag, bool bGeometricMethod, double dSigma1,
                                  double dSigma2) {
    double dRho = dV2 / dV1;
    if (fabs(dCosA) > MIN_ANGLE) {
        dCosA = (dCosA > 0) ? MIN_ANGLE : -MIN_ANGLE;
    }
    double dSinA = sqrt(1 - pow(dCosA, 2));
    double dLambda = dSinA / sqrt(pow(dRho, 2) - 2 * dRho * dCosA * (bFlag) ? 1 : -1 + 1);
    double dMu = dLambda * dV2 * fabs(dT2 - dT1);
    double dSigma = dLambda * (bGeometricMethod) ? A_BAR * sqrt(1 + pow(dRho, 2)) : dV2 * sqrt(pow(dSigma1, 2) +
                                                                                               pow(dSigma2, 2));
    double dRight = (MIN_SEPARATION_DISTANCE * K - dMu) / (sqrt(2.0) * dSigma);
    double dLeft = dMu / (sqrt(2.0) * dSigma);
    *pdDelayTime = MIN_SEPARATION_DISTANCE * K / (dLambda * dV2) - fabs(dT2 - dT1);
    double dConflictProbability = fabs(0.5 * (boost::math::erf(dRight) + boost::math::erf(dLeft)));
    return dConflictProbability;
}

double Route::calculateProbabilityAndDelay(int iIndex1, Route *pRoute2, int iIndex2, double *pdDelayTime, bool bFlag1,
                                           bool bFlag2, bool bGeometricMethod, double dSigma1,
                                           double dSigma2) {
    double dT1 = getArrivingTimeAtPoint(iIndex1 + bFlag1 ? 0 : 1);
    double dT2 = pRoute2->getArrivingTimeAtPoint(iIndex2 + bFlag2 ? 0 : 1);
    double dV1 = getVelocityFromPoint(iIndex1 + bFlag1 ? 0 : 1);
    double dV2 = pRoute2->getVelocityFromPoint(iIndex2 + bFlag1 ? 0 : 1);
    double dCosA = getCosA(getPositionAtPoint(iIndex1), getPositionAtPoint(iIndex1 + bFlag1 ? -1 : 1),
                           pRoute2->getPositionAtPoint(iIndex2 + bFlag2 ? -1 : 1));
    return probabilityAndDelay(dT1, dT2, dV1, dV2, dCosA, pdDelayTime, bFlag1 == bFlag2, bGeometricMethod, dSigma1,
                               dSigma2);
}

double Route::CalculationProbabilityAndDelay(int iIndex1, Route *pRoute2, int iIndex2, double *pdDelayTime,
                                             bool *pbWait, bool bGeometricMethod, double dSigma1, double dSigma2) {
    if (*getPointAtI(iIndex1) != *pRoute2->getPointAtI(iIndex2)) {
        *pdDelayTime = 0;
        *pbWait = true;
        return 0;
    }
    DoubleVector vdConflictProbabilityList;
    DoubleVector vdDelayTime;
    double pdDelay;
    double dProbability;
    if (iIndex1 > 0 && iIndex1 < getPointListSize() - 1) {
        if (iIndex2 > 0 && iIndex2 < pRoute2->getPointListSize() - 1) {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, false,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
            if (getArrivingTimeAtPoint(iIndex1) > pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, false,
                                                            bGeometricMethod,
                                                            dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            } else {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, true,
                                                            bGeometricMethod,
                                                            dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            }
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, true,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
        } else if (iIndex2 == 0) {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, false,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
            if (getArrivingTimeAtPoint(iIndex1) > pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, false,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            }
        } else {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, true,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
            if (getArrivingTimeAtPoint(iIndex1) < pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, true,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            }
        }
    } else if (iIndex1 == 0) {
        if (iIndex2 > 0 && iIndex2 < pRoute2->getPointListSize() - 1) {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, false,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
            if (getArrivingTimeAtPoint(iIndex1) < pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, true,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            }
        } else if (iIndex2 == 0) {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, false,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
        } else {
            if (getArrivingTimeAtPoint(iIndex1) < pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, false, true,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            } else {
                vdDelayTime.push_back(0);
                vdConflictProbabilityList.push_back(0);
            }
        }
    } else {
        if (iIndex2 > 0 && iIndex2 < pRoute2->getPointListSize() - 1) {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, true,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
            if (getArrivingTimeAtPoint(iIndex1) > pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, false,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            }
        } else if (iIndex2 == 0) {
            if (getArrivingTimeAtPoint(iIndex1) > pRoute2->getArrivingTimeAtPoint(iIndex2)) {
                dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, false,
                                                            bGeometricMethod, dSigma1, dSigma2);
                vdDelayTime.push_back(pdDelay);
                vdConflictProbabilityList.push_back(dProbability);
            } else {
                vdDelayTime.push_back(0);
                vdConflictProbabilityList.push_back(0);
            }
        } else {
            dProbability = calculateProbabilityAndDelay(iIndex1, pRoute2, iIndex2, &pdDelay, true, true,
                                                        bGeometricMethod, dSigma1, dSigma2);
            vdDelayTime.push_back(pdDelay);
            vdConflictProbabilityList.push_back(dProbability);
        }
    }
    *pdDelayTime = *std::max_element(vdDelayTime.begin(), vdDelayTime.end());
    *pbWait = getArrivingTimeAtPoint(iIndex1) > pRoute2->getArrivingTimeAtPoint(iIndex2);
    double dConflictProbability = std::accumulate(vdConflictProbabilityList.begin(), vdConflictProbabilityList.end(),
                                                  0);
    return dConflictProbability;
}
