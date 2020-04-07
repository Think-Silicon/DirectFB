#!/bin/bash

#stop on error
set -e

#verbose
set -x

source cross-env.sh 

make -j5
find . -name .libs | xargs -l chmod a+u
sudo make install

