#!/bin/bash

# Download and extract Boost
wget https://github.com/boostorg/boost/releases/download/boost-1.81.0/boost-1.81.0.zip
unzip boost-1.81.0.zip
cd boost-1.81.0

# Run bootstrap
chmod +x bootstrap.sh
chmod +x ./tools/build/src/engine/build.sh
./bootstrap.sh

# Build using b2 & Install Boost
./b2 --with-system --with-program_options address-model=64 variant=release install --prefix=./boost-1.81
