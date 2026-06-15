# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`mgclient` is a C library implementing the Bolt protocol client for [Memgraph](https://www.memgraph.com) (also compatible with Neo4j Bolt). The core library is C11. A header-only C++17 wrapper (`mgclient_cpp`) sits on top, and it can also be compiled to WebAssembly via Emscripten.

## Build & test

Standard build (produces `libmgclient.a` + `libmgclient.so`/`.dylib`):

```
mkdir build && cd build
cmake ..
make
```

With tests enabled (this also forces `BUILD_CPP_BINDINGS=ON`):

```
cmake -DBUILD_TESTING=ON -DBUILD_TESTING_INTEGRATION=ON ..
make
ctest
```

- **Run a single test:** `ctest -R encoder` (test names: `value`, `encoder`, `decoder`, `client`, `transport`, `allocator`, `unit_mgclient_value`, plus `integration_basic_c`, `integration_basic_cpp`, `example_*`).
- **Unit tests only (no running Memgraph):** `ctest -E "example|integration"`. The `integration_*` and `example_*` tests require a live Memgraph on `127.0.0.1:7687`.
- **OpenSSL not found:** pass `-DOPENSSL_ROOT_DIR=...` (see README for macOS/Windows specifics).
- **WASM build (Linux only):** `cmake .. -DWASM=ON && make` → emits `mgclient.js` + `mgclient.wasm`. WASM uses WebSocket transport and has no OpenSSL dependency.

## Formatting

`./tool/format.sh` runs `clang-format` (Google style, 80-col, right-aligned pointers) over all `*.c/*.h/*.cpp/*.hpp` files **in place** and fails if anything changed. CI runs this on every push/PR as the `clang_check` job — formatting failures break CI, so run it before committing.

Coverage report: `./tool/coverage.sh` (requires a build with `-DENABLE_COVERAGE=ON`; uses `llvm-profdata`/`llvm-cov`).

## Architecture

The library is layered. Public symbols are exported via the `MGCLIENT_EXPORT` macro (generated `mgclient-export.h`); everything in `src/*.h` is internal.

- **`include/mgclient.h`** — the entire public C API and its Doxygen documentation. The big comment block at the top is the authoritative spec for the **ownership model** (read it before touching value/container code): non-const pointer returns transfer ownership to the caller; const pointer returns are read-only views valid only while the owner lives; insert functions steal ownership of inserted values. Getting this wrong causes double-frees.

- **Session layer** (`mgsession.c`, `mgsession.h`) — `mg_session` is the connection object and is *single-command-at-a-time*: you `mg_session_run` a query, then `mg_session_pull` rows until it returns 0 before running anything else. `mg_connect` performs the Bolt handshake and HELLO. The session struct holds the in/out buffers, the negotiated Bolt `version`, transaction state (`explicit_transaction`), and two allocators (one general, one scoped to decoding).

- **Encoder / decoder** (`mgsession-encoder.c`, `mgsession-decoder.c`) — serialize/deserialize Bolt messages and values over the chunked Bolt framing. All Bolt markers, struct signatures, and message signatures live in `src/mgconstants.h` — this is the reference when adding a new value type or Bolt message. Note that Bolt has multiple protocol versions and some value types (temporal types, ZonedDateTime) are version-gated; check how `session->version` is consulted.

- **Transport layer** (`mgtransport.c`, `mgtransport.h`) — polymorphic `mg_transport` struct of function pointers (`send`/`recv`/`destroy`/suspend hooks). Three implementations: `mg_raw_transport` (plain socket), `mg_secure_transport` (OpenSSL/SSL, supports peer pubkey fingerprint verification via trust callback), and the WASM WebSocket path. The session talks only to the `mg_transport` interface and is agnostic to which one is in use.

- **Socket layer** — OS-specific, selected at CMake configure time: `src/{linux,apple,windows}/mgsocket.c` (matching `mgcommon.h` per platform). The build picks exactly one based on `MGCLIENT_ON_{LINUX,APPLE,WINDOWS}`.

- **Values** (`mgvalue.c`, `mgvalue.h`) — implementation of all Bolt data types (`mg_value`, `mg_string`, `mg_list`, `mg_map`, `mg_node`, `mg_relationship`, `mg_path`, temporal types, points). This is the largest file and where the ownership rules from `mgclient.h` are enforced.

- **Allocator** (`mgallocator.c`, `mgallocator.h`) — pluggable `mg_allocator` interface; the library allocates through it rather than calling `malloc` directly.

- **C++ wrapper** (`mgclient_cpp/include/`, header-only) — `mg::Client` (RAII connection with `Client::Connect(params)`), `mg::Value`, and an exception hierarchy (`MgException` → `ClientException`/`TransientException`/`DatabaseException`). Pure wrapper over the C API; no separate compiled library.

## Tests

- `tests/*.cpp` — unit tests (GTest, fetched via `FetchContent` at `release-1.8.1`). They link against `mgclient-static` and the C++ bindings.
- `tests/integration/` — require a running Memgraph instance; gated behind `BUILD_TESTING_INTEGRATION`.
- `client.cpp` mocks `mg_secure_transport_init` using the linker `--wrap` mechanism (`-Wl,--wrap=` on Linux, `-Wl,-alias,` on Apple) — see `tests/CMakeLists.txt`. If you rename that function, update the wrap flags too.
- `examples/` (`basic.c`, `basic.cpp`, `advanced.cpp`) are also compiled and registered as ctest tests; they double as API usage references.

## Versioning gotcha

`CMakeLists.txt` carries two independent version numbers: `project(... VERSION x.y.z)` and `mgclient_SOVERSION`. A minor version bump can mean ABI incompatibility — the SOVERSION must be bumped manually when the ABI changes (it is not derived from the project version).
