[![Actions Status](https://github.com/memgraph/mgclient/workflows/CI/badge.svg)](https://github.com/memgraph/mgclient/actions)

# mgclient

mgclient is a C library interface for [Memgraph](https://www.memgraph.com)
database.

## Building and installing on Apple

To build and install mgclient from source you will need:
   - CMake version >= 3.8
   - OpenSSL version >= 1.0.2
   - Apple LLVM/clang >= 8.0.0

Once everything is in place, create a build directory inside the source
directory and configure the build by running CMake from it:

```
mkdir build
cd build
cmake ..
```

NOTE: Dealing with OpenSSL might be a bit tricky. If OpenSSL is not available
on the system, please use, e.g., [brew](https://brew.sh/) package manager to
install OpenSSL with the following command:

```
brew install openssl@1.1
```

If `cmake` can't locate OpenSSL, please set `OPENSSL_ROOT_DIR` to a valid path.
An examples follows:

```
cmake -DOPENSSL_ROOT_DIR="$(ls -rd -- /usr/local/Cellar/openssl@1.1/* | head -n 1)" ..
```

After running CMake, you should see a Makefile in the build directory. Then you
can build the project by running:

```
make
```

This will build two `mgclient` library flavours: a static library (named
`libmgclient.a`) and a shared library (named `libmgclient.dylib`).

To install the libraries and corresponding header files run:

```
make install
```

This will install to system default installation directory. If you want to
change this location, use `-DCMAKE_INSTALL_PREFIX` option when running CMake.

## Building and installing on Linux

To build and install mgclient from source you will need:
   - CMake version >= 3.8
   - OpenSSL version >= 1.0.2
   - gcc >= 8 or clang >= 8

To install minimum compile dependencies on Debian / Ubuntu:

```
apt-get install -y git cmake make gcc g++ libssl-dev
```

On RedHat / CentOS / Fedora:

```
yum install -y git cmake make gcc gcc-c++ openssl-devel
```

Once everything is in place, create a build directory inside the source
directory and configure the build by running CMake from it:

```
mkdir build
cd build
cmake ..
```

After running CMake, you should see a Makefile in the build directory. Then you
can build the project by running:

```
make
```

This will build two `mgclient` library flavours: a static library (usually
named `libmgclient.a`) and a shared library (usually named `libmgclient.so`).

To install the libraries and corresponding header files run:

```
make install
```

This will install to system default installation directory. If you want to
change this location, use `-DCMAKE_INSTALL_PREFIX` option when running CMake.

If you want to build and run tests, in the build directory run:

```
cmake -DBUILD_TESTING=ON -DBUILD_TESTING_INTEGRATION=ON ..
ctest
```

## Building and installing on Windows

To build `mgclient` on Windows, MINGW environment should be used.
   - Install MSYS2 from https://www.msys2.org/.
   - Install MinGW toolchain with the following command:
     ```
     pacman -S --needed git base-devel mingw-w64-i686-toolchain mingw-w64-x86_64-toolchain mingw-w64-i686-cmake mingw-w64-x86_64-cmake mingw-w64-i686-openssl mingw-w64-x86_64-openssl
     ```

Once the environment is ready, please run:

```
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --target install
```
## Building WASM (linux only)
Compiling `mgclient` requires the Emscripten sdk found in https://github.com/emscripten-core/emsdk and OpenSSL WASM.
  - Clone the emsdk repo above and do `./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh`
  - Clone openssl repo found in https://github.com/openssl/openssl and compile it to WASM with `emconfigure ./Configure -no-asm -no-tests && make -j8`
  - Create a build directory under, i.e., `mgclient/build` and then do (replace `PATH_TO` to the right path):
    ```
    emcmake cmake .. -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR=PATH_TO/openssl/apps/ -DOPENSSL_INCLUDE_DIR=PATH_TO/openssl/include/ -DOPENSSL_SSL_LIBRARY=PATH_TO/openssl/libssl.a -DOPENSSL_CRYPTO_LIBRARY=PATH_TO/openssl/libcrypto.a

    emmake make

    emcc src/libmgclient.a -o mgclient.js -s ASYNCIFY=1 -s MODULARIZE -s EXPORT_NAME="load_mgclient" --shared-memory --no-entry -s USE_PTHREADS=1 -s SOCKET_DEBUG=1 -s WEBSOCKET_SUBPROTOCOL="binary" -s EXPORTED_FUNCTIONS='_mg_init, _mg_finalize, _mg_session_params_make, _mg_session_params_destroy, _mg_session_params_set_host, _mg_session_params_set_port, _mg_session_params_set_sslmode, _mg_session_error, _mg_session_run, _mg_session_fetch, _mg_session_pull, _mg_connect' -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,getValue,setValue" -L/home/rim/Desktop/openssl/ -lssl -lcrypto

    ```
Now there should be an `mgclient.js` and an `mgclient.wasm` found in `mgclient/build/`

## Using the library

The library provides header files located under the include folder. All library
functionality is documented in these files in Doxygen format. You can also
build HTML version of the documentation by running `doxygen` command from
project root directory.

## Examples

All the examples of the usage of the mgclient are contained in the
[examples](examples) folder, including the C++ wrapper.
