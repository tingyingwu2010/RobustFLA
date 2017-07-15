//
// Created by martinwang on 27/04/17.
//

#ifndef NETWORK_H
#define NETWORK_H

#include "Flight.h"

class Network {
public:
    static double PERIOD_LENGTH;
    /**
     * Default Constructor.
     */
    Network(): vpFlightsList(),vpAirportsList(),vpWayPointsList(), viFlightLevelsList(){}

    /**
     * Destructor.
     */
    virtual ~Network(){
//        for(auto &&flight : vpFlightsList){
//            delete flight;
//        }
//        for(auto &&airport: vpAirportsList){
//            delete airport;
//        }
//        for(auto &&wayPoint: vpWayPointsList){
//            delete wayPoint;
//        }
        vpFlightsList.clear();
        vpAirportsList.clear();
        vpWayPointsList.clear();
        viFlightLevelsList.clear();
    }

    /**
     * Add a new flight in the air traffic management network.
     * @param pFlight     The pointer of a new flight
     */
    void addNewFlight(Flight *pFlight){
        vpFlightsList.push_back(pFlight);
    }

    /**
     * Add a new airport in the air traffic management network.
     * @param pAirport     The pointer of a new airport
     */
    void addNewAirport(Airport *pAirport){
        vpAirportsList.push_back(pAirport);
    }

    /**
     * Add a new wayPoint in the air traffic management network.
     * @param pWayPoint     The pointer of a new wayPoint
     */
    void addNewWayPoint(WayPoint *pWayPoint){
        vpWayPointsList.push_back(pWayPoint);
    }

    /**
     * Get the number of airports in the air traffic management network.
     * @return the number of airports in the air traffic management network.
     */
    int getNbAirports() const{
        return (int)vpAirportsList.size();
    }

    /**
     * Get the number of wayPoints in the air traffic management network.
     * @return the number of wayPoints in the air traffic management network.
     */
    int getNbWayPoints() const{
        return (int)vpWayPointsList.size();
    }

    /**
     * Get the number of flights in the air traffic management network.
     * @return the number of flights in the air traffic management network.
     */
    int getNbFlights() const{
        return (int)vpFlightsList.size();
    }


    /**
     * Get the number of levels in the air traffic management network.
     * @return the number of levels in the air traffic management network.
     */
    int getNbLevels() const{
        return (int)viFlightLevelsList.size();
    }

    /**
     * Find the airport object by its given code.
     * @param sCode     The code of an airport.
     * @return The pointer of a corresponding airport.
     */
    Airport *findAirportByCode(String sCode) {
        return findByCode<Airport>(vpAirportsList, sCode);
    }

    /**
     * Find the wayPoint object by its given code.
     * @param sCode     The code of a wayPoint.
     * @return  The pointer of a corresponding wayPoint.
     */
    WayPoint *findWayPointByCode(String sCode) {
        return findByCode<WayPoint>(vpWayPointsList, sCode);
    }

    /**
     * Getter for vpFlightsList.
     * @return the list of flights in the air traffic management network.
     */
    const FlightVector &getFlightsList() const {
        return vpFlightsList;
    }

    /**
     * Getter for viFlightLevelsList.
     * @return the list of flight levels in the air traffic management network.
     */
    const LevelVector &getFlightLevelsList() const{
        return viFlightLevelsList;

    }

    /**
     * Getter for vpAirportsList.
     * @return the list of airports in the air traffic management network.
     */
    const AirportVector &getAirportsList() const {
        return vpAirportsList;
    }

    /**
     * Getter for vpWayPonitsList.
     * @return the list of wayPoints in the air traffic management network.
     */
    const WayPointVector &getWayPointsList() const {
        return vpWayPointsList;
    }

    /**
     * Init the flight level list.
     */
    void InitFlightLevelsList();
private:
    /**
     * The flight list in the air traffic management network.
     */
    FlightVector vpFlightsList;

    /**
     * The airport list in the air traffic management network.
     */
    AirportVector vpAirportsList;

    /**
     * The wayPoint list in the air traffic management network.
     */
    WayPointVector vpWayPointsList;

    /**
     * The flight levels list in the air traffic management network.
     */
    LevelVector viFlightLevelsList;
};
#endif //NETWORK_H
