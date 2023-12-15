#!/bin/bash

set -e

script_dir=$(dirname $0)
script_dir=$(realpath "$script_dir")

cd "$script_dir" && ./canon-intervalometer --web-root "./web_root"
