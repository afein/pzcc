#!/bin/bash

components=$@

make

for component in $components;
do
    echo ""
    echo "==========  Now testing $component ============"

    for testcase in tests/$component/*.pzc;
    do echo "========== $testcase ============"
        ./pzcc -i $testcase
    done
done

#make clean
