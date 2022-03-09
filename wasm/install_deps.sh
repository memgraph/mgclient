#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
git clone https://github.com/emscripten-core/emsdk.git ${DIR}/../build/emsdk
${DIR}/../build/emsdk/emsdk install latest
