#!/bin/bash
set -e

RUN=$1

cd bench
mkdir build || true
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
echo "RUN = $RUN"
make
if [ "$RUN" = "TRUE" ]; then
    $PWD/density_bench
fi
