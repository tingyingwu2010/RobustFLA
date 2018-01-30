#include "../include/Solution.h"
#include <boost/filesystem.hpp>
void EchoUsage(){
    std::cout
            << "[USAGE] RobustFLA airport_json_File beacon_json_file flight_json_file pa period_length feasible_list_size method_mode epsilon coefficient_Pi nbIterations minAdmissibleCost maxAdmissibleCost displayMode"
            << std::endl
            << "method_mode 0 \t\t\t Deterministic Method" << std::endl
            << "method_mode 1 \t\t\t Hoeffding Inequalities Method" << std::endl
            << "method_mode 2 \t\t\t Monte-Carlo Simulation Method" << std::endl
            << "method_mode 3 \t\t\t Gaussian Method" << std::endl
            << "period_length \t\t\t An integer, the length of period that consider en-route conflict occurs at a wayPoint."
            << std::endl
            << "epsilon \t\t\t An integer in [0, 100],  the tolerance of infeasibility of separated constraints in FLA problem."
            << std::endl
            << "feasible_list_size \t\t\t An integer, the size of feasible flight level list." << std::endl
            << "coefficient_Pi \t\t\t An integer in [0, 100], the percentage of aircraft's flight time to induce an admissible penal cost"
            << std::endl
            ;
}

int main(int argc, char *argv[]) {
    using std::invalid_argument;
    try {
        if (argc != 14) {
            EchoUsage();
            abort();
        }
        auto percentileSup = boost::lexical_cast<int>(argv[4]);
        std::stringstream modifiedFlightFileName;
        modifiedFlightFileName << "../data/flight_" << percentileSup <<"_-.json";
        String flightName = modifiedFlightFileName.str();
        bool modeDisplay = boost::lexical_cast<int>(argv[13]) == 1;
        double epsilon = boost::lexical_cast<int>(argv[8]) / 100.0;
        auto feasibleSize = boost::lexical_cast<int>(argv[6]);
        double coefPi = boost::lexical_cast<int>(argv[9]) / 100.0;
        auto modeMethod = boost::lexical_cast<int>(argv[7]);
        auto nbIterations = boost::lexical_cast<int>(argv[10]);
        auto minAdmissibleCost = boost::lexical_cast<int>(argv[11]);
        auto maxAdmissibleCost = boost::lexical_cast<int>(argv[12]);
        if (nbIterations < 0) {
            nbIterations = 1000;
        }
        if (minAdmissibleCost < 0) {
            minAdmissibleCost = 0;
        }
        Network network;
        Input input(argv[1], argv[2], exists(flightName) ? flightName:argv[3]);
        input.initNetwork(&network, exists(flightName), modeDisplay);
        network.initialize(coefPi, feasibleSize, modeDisplay);
//        for(auto &&flight:network.getFlightsList()){
//            std::cout << flight->getDuration() << std::endl;
//        }
        String dataFilename = !exists(flightName) ? network.generateFlightsSup(percentileSup, TIMEOFFSETSUP) : flightName;
        Network::PERIOD_LENGTH = boost::lexical_cast<int>(argv[5]) / 1.00;
        if (modeMethod == 100) {
            ApproximateFLA(&network, dataFilename, epsilon, coefPi, minAdmissibleCost, maxAdmissibleCost, feasibleSize,
                           0, percentileSup, modeDisplay, 0);
            ApproximateFLA(&network, dataFilename, epsilon, coefPi, minAdmissibleCost, maxAdmissibleCost, feasibleSize,
                           1, percentileSup, modeDisplay, 0);
            ApproximateFLA(&network, dataFilename, epsilon, coefPi, minAdmissibleCost, maxAdmissibleCost, feasibleSize,
                           2, percentileSup, modeDisplay, nbIterations);
            ApproximateFLA(&network, dataFilename, epsilon, coefPi, minAdmissibleCost, maxAdmissibleCost, feasibleSize,
                           3, percentileSup, modeDisplay, 0);
        } else {
            ApproximateFLA(&network, dataFilename, epsilon, coefPi, minAdmissibleCost, maxAdmissibleCost, feasibleSize,
                           modeMethod, percentileSup, modeDisplay, nbIterations);
        }
    }
    catch (const invalid_argument &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}