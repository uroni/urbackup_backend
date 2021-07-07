#!/bin/bash

# Until CodeBuild supports macOS, this script is just used by Travis.

set -e

mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=../../install $@ ../
make
make test
make install
LSAN_OPTIONS=verbosity=1:log_threads=1 ctest --output-on-failure

