#!/usr/bin/env bash
for a in 15 ; do
    for b in 45; do
        for p in 9957; do
            for w1 in 33;  do
                for w2 in 20; do
                    for p in 5 ; do
                        for pa in 0 15; do
                            for periodLength in 30 ; do
                                for feasibleSize in 3 ; do
                                    for method in 0 1 3; do
                                        for epsilon in 25 20 15 10 5; do
                                            for coefPi in 15 10 8 5; do
                                                ./runProgram.sh ${a} ${b} 9957 ${w1} ${w2} ${p} ${pa} 1 0 ${periodLength} ${feasibleSize} ${method} ${epsilon} ${coefPi} 1000 1 0
                                            done
                                        done
                                    done
                                    for method in 2; do
                                        for epsilon in 25 20 15 10 5; do
                                            for coefPi in 15 10 8 5; do
                                                ./runProgram.sh ${a} ${b} 9957 ${w1} ${w2} ${p} ${pa} 1 0 ${periodLength} ${feasibleSize} ${method} ${epsilon} ${coefPi} 1000 1 0
                                            done
                                        done
                                    done
                                done
                            done
                        done
                    done
                done
            done
        done
    done
done
