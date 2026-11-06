# Security Policy

## Supported versions

Only the latest tagged release and the current `main` branch receive security
fixes.

## Reporting a vulnerability

Please do not open a public issue for security reports. Use GitHub's private
vulnerability reporting on this repository (Security tab -> Report a
vulnerability), or contact a maintainer directly.

Include:

* The affected version or commit.
* The exact command line and input file description needed to reproduce.
* The full console output or JSON report (`--report`).

## Scope

Ryty processes untrusted executable files. Of special interest:

* Memory safety issues in the ELF/PE parsers, the NID filter, and the
  instruction decoder/rewriter.
* Generated trampoline stubs corrupting guest state (register preservation,
  stack balance, red-zone use).
* Path handling around `--rpath`, `--report` and guest module output.

## What is in scope for disclosure

Hardening reports that improve the "fail with a clear error instead of
misbehaving" guarantee are welcome as regular public issues when they do not
expose an exploitable path.
