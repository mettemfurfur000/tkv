# TKV Adoption Roadmap

This roadmap prioritizes making TKV reliable and easy to consume before adding
language-specific integrations. Each phase should preserve the existing C API
unless a deliberate, documented API change is needed.

## 1. Stabilize the core

- Replace parser `printf` diagnostics with structured error reporting that
  callers can inspect, while retaining a convenient parsing entry point.
- Define ownership and lifetime rules for parsed objects, nested objects,
  arenas, and serialized buffers.
- Add tests for malformed and truncated inputs, numeric boundaries, nested
  objects, and allocation failures where practical.
- Add a fuzz target for the text parser and stream reader.

**Complete when:** errors are actionable without parsing standard output,
ownership is documented, and tests/fuzzing cover untrusted input paths.

## 2. Define a portable binary format

- Treat the current in-memory object layout as an implementation detail; it
  contains native-layout fields and nested pointers and should not be treated
  as a portable file or network format.
- Specify a versioned wire format with explicit byte order, lengths, type
  identifiers, and nested-object representation.
- Add encode/decode APIs that operate on caller-provided buffers or streams.
- Test golden byte sequences and compatibility between supported versions.

**Complete when:** the format is specified independently of C structs and
passes round-trip and golden-vector tests.

## 3. Make the C library easy to build and install

- Add CMake alongside Make, including install/export rules and a CTest target.
- Provide pkg-config metadata for installed consumers.
- Make the temlib dependency explicit and document static/shared linking.
- Add CI for supported compilers and operating systems; test both build systems.

**Complete when:** a consumer can find, link, and test an installed TKV package
using standard CMake or pkg-config workflows.

## 4. Improve distribution and documentation

- Document the text grammar, supported types, limits, and error behavior.
- Add small end-to-end examples for parsing, reading values, mutating objects,
  text serialization, and binary encoding.
- Publish versioned releases with changelog, license, and migration notes.
- Add reproducible release artifacts for supported platforms.

**Complete when:** a new user can install a release and complete a common task
from the documentation without needing to inspect implementation sources.

## 5. Add language bindings

Start only after the C API and binary format are stable.

- **Python:** provide a small, idiomatic package with parse/serialize helpers,
  value access, clear exceptions, and wheels for supported platforms.
- **Rust:** provide a safe wrapper with ownership-aware types and a documented
  strategy for building or locating the native library.
- Keep bindings thin over the stable C ABI; avoid exposing internal object
  memory layouts.
- Add language-specific examples and round-trip tests against shared fixtures.

**Complete when:** each binding has independent installation instructions,
automated tests, and interoperability tests using the same fixtures.

## Suggested order

1. Core errors, ownership documentation, and parser tests.
2. Portable wire-format specification and implementation.
3. CMake, pkg-config, and cross-platform CI.
4. User documentation and versioned releases.
5. Python and Rust bindings.

The order is intentional: bindings and packaged artifacts are costly to
maintain if the underlying error, ownership, or binary-format contracts are
still changing.
