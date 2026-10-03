# CI

`.github/workflows/CI.yml` is the single workflow file. Detailed behaviour is
documented in [`ci/README.md`](../../ci/README.md); the short version:

* **linux** (ubuntu-24.04): Qt 6.8.2 via `install-qt-action`, `linux-release`
  preset, full `ctest`, GUI bundled into a self-contained AppImage with the
  Qt6 runtime embedded, headless smoke test, zip with the CLI + AppImage.
* **windows** (windows-latest, MSYS2 UCRT64): `windows-release` preset, full
  `ctest`, GUI packaged with `windeployqt6` (every Qt6 DLL/plugin next to
  `ryty-gui.exe`), zip with the CLI + GUI bundle.
* **linux-cli** (ubuntu-24.04, no Qt): builds the `linux-release-cli` preset
  to guard the graceful GUI skip.

Every push and pull request produces artifacts; `v*` tags additionally
publish a GitHub release with both platform zips. The GUI bundling recipe was
validated end-to-end: the packaged AppImage runs headless
(`QT_QPA_PLATFORM=offscreen`) with zero Qt on the host.
