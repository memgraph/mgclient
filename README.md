[![Actions Status](https://github.com/memgraph/mgclient/workflows/CI/badge.svg)](https://github.com/memgraph/mgclient/actions)

# mgclient

mgclient is a C library interface for [Memgraph](https://www.memgraph.com)
database.

# Building and installing

To build and install mgclient from source you will need:
   - CMake version >= 3.8
   - OpenSSL version >= 1.0.2
   - C compiler supporting C11
   - C++ compiler supporting C++17

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

If you want to run tests, just type `ctest` in your build directory.

# Using the library

The library provides a single header file `mgclient.h`. All library
functionality is documented in that file in Doxygen format. You can also build
HTML version of the documentation by running `doxygen` command from project
root directory.

Here's a simple client program making a connection to Memgraph server and
executing a single query:

# Examples
All the examples of the usage of the mgclient are contained in the
[examples](examples) folder.
