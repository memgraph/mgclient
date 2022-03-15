#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
git clone https://github.com/emscripten-core/emsdk.git ${DIR}/emsdk
${DIR}/emsdk/emsdk install latest
${DIR}/emsdk/emsdk activate latest
