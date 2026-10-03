# Pull requests

- Follow the [coding conventions](../docs/dev/CONVENTIONS.md) (naming, no
  comments except technical debt, Conventional Commits).
- Every function either does exactly what it is supposed to or throws.
- New lowering stubs need verification coverage in
  `core/relinker/codegen/tests/ShaLoweringVerificationTests.cpp` or
  `Amd64OnlyConverterTests.cpp` before merge.
- `ctest` must pass on the `linux-release` preset (16 tests, including the
  hardware-verified SHA lowering suite when the host supports SHA-NI).
