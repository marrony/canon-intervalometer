#!/bin/bash

set -ex

script_dir=$(dirname $0)
# script_dir=$(realpath "$script_dir")

cd "$script_dir"

./main
