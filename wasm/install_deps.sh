#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
git clone https://github.com/emscripten-core/emsdk.git ${DIR}/emsdk
${DIR}/emsdk/emsdk install 3.1.9
${DIR}/emsdk/emsdk activate 3.1.9
