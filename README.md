# mgclient

mgclient is a C library interface for [Memgraph](https://www.memgraph.com)
database.

# Building and installing

To build and install mgclient from source you will need:

   - CMake version >= 3.4
   - OpenSSL version >= 1.0.2
   - C compiler supporting C11

Additionally, if you want to build tests, you'll need:
   - C++ compiler supporting C++14

Once everything is place, create a build directory inside the source directory
and configure the build by running CMake from it:

```
$ mkdir build
$ cd build
$ cmake ..
```

If you do not want to build tests, pass `-DBUILD_TESTING=OFF` to CMake like this:

```
$ cmake -DBUILD_TESTING=OFF ..
```

After running CMake, you should see a Makefile in the build directory. Then you
can build the project by running:

```
make
```

This will build two `mgclient` library flavours: a static library (usually
named `libmgclient.a`) and a shared library (usually named `libmgclient.so`).

To install the libraries and corresponding header files run `make install`.
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

```c
#include <stdio.h>
#include <stdlib.h>

#include <mgclient.h>

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s [host] [port] [query]\n", argv[0]);
    exit(1);
  }

  mg_session_params *params = mg_session_params_make();
  if (!params) {
    fprintf(stderr, "failed to allocate session parameters\n");
    exit(1);
  }
  mg_session_params_set_host(params, argv[1]);
  mg_session_params_set_port(params, (uint16_t)atoi(argv[2]));

  mg_session *session = NULL;
  int status = mg_connect(params, &session);
  mg_session_params_destroy(params);
  if (status < 0) {
    printf("failed to connect to Memgraph: %s\n", mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  if (mg_session_run(session, argv[3], NULL, NULL) < 0) {
    printf("failed to execute query: %s\n", mg_session_error(session));
    mg_session_destroy(session);
    return 1;
  }

  mg_result *result;
  int rows = 0;
  while ((status = mg_session_pull(session, &result)) == 1) {
    rows++;
  }

  if (status < 0) {
    printf("error occurred during query execution: %s\n",
           mg_session_error(session));
  } else {
    printf("query executed successfuly and returned %d rows\n", rows);
  }

  mg_session_destroy(session);
  return 0;
}
```
