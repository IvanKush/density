#!/bin/bash

RUN=$1
DEBUG=&2
COVERAGE=&3

echo "RUN = $RUN, DEBUG = $DEBUG, COVERAGE = $COVERAGE"

if [ "$DEBUG" = "TRUE" ]; then
    MAKE_ARGS+=" -DCMAKE_BUILD_TYPE=Debug"
fi

if [ "$COVERAGE" = "TRUE" ]; then
    GCC_OPTIONS+=" -fprofile-arcs -ftest-coverage"
fi

cd test
mkdir build
cd build
cmake .. "$MAKE_ARGS" -DCOMPILER_EXTRA:STRING=MAKE_ARGS
make
if [ "$RUN" = "TRUE" ]; then
    sudo make install
    density_test
fi
