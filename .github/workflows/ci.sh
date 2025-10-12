#!/usr/bin/env bash

set -eux

PROJECT_DIR=$PWD
cd $PROJECT_DIR/src/os
./generate.py
./build.py
./run.py
cd $PROJECT_DIR
