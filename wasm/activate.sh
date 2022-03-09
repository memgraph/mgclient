#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
EMSDK="${DIR}/../build/emsdk"
${EMSDK}/emsdk activate latest
source ${EMSDK}/emsdk_env.sh
