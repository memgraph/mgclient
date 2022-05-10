#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
git clone https://github.com/emscripten-core/emsdk.git ${DIR}/emsdk
${DIR}/emsdk/emsdk install sdk-releases-upstream-edabe25af34554d19c046078f853999b074259ca-64bit
${DIR}/emsdk/emsdk activate sdk-releases-upstream-edabe25af34554d19c046078f853999b074259ca-64bit
