# Ryty CI - one workflow, bundled GUIs

`.github/workflows/CI.yml` is the single workflow file for the project. It
builds, tests and packages Ryty on Linux and Windows, and ships the Qt6 GUI
**with the Qt6 runtime files bundled** so it actually runs on end-user
machines.

## Outputs

| Trigger                    | Result                                                            |
| -------------------------- | ----------------------------------------------------------------- |
| push to `main`/`master`    | build + tests + artifacts (per-platform zips)                     |
| pull request               | build + tests + artifacts                                         |
| tag `v*` (e.g. `v1.0`)     | same as above **plus a GitHub release** with both platform zips    |

Artifacts / release assets:

* `Ryty-<ver>-linux-x86_64.zip`
  * `ryty` - CLI binary
  * `Ryty-GUI-<ver>-linux-x86_64.AppImage` - GUI with the full Qt6 runtime
    (libraries + platform/imageformat plugins + qt.conf) embedded via
    `linuxdeploy` + `linuxdeploy-plugin-qt`. Runs on Ubuntu 24.04+/Debian 13+/
    Fedora 40+ class systems without any Qt installation.
* `Ryty-<ver>-windows-x86_64.zip`
  * `ryty.exe` - CLI binary
  * `ryty-gui.exe` + every required Qt6 DLL and plugin, deployed by
    `windeployqt6` (plus MinGW runtime DLLs) - unzip and double-click.

## How the GUI bundling works

* **Linux**: Qt 6.8.2 (pinned once as `env.QT_VERSION` in the workflow) is
  installed with `jurplel/install-qt-action`; after build+tests, `linuxdeploy`
  deploys the executables' shared-library closure, `--plugin qt` adds the Qt
  libraries and plugins (xcb platform plugin and its X11 stack), the
  `offscreen` platform plugin is added for headless usage, and
  `--output appimage` produces the single-file AppImage. OpenGL/EGL stay on
  the host system by design (they are driver-specific) - standard AppImage
  practice.
* **Windows**: Qt 6 comes from the MSYS2 UCRT64 repository (same ABI as the
  UCRT64 toolchain the `windows-release` preset targets); `windeployqt6`
  copies the exact set of DLLs/plugins the GUI imports next to the executable.
* The bundling recipe was validated end-to-end on a live system: a Qt6
  Widgets application bundled with this exact pipeline ran headless
  (`QT_QPA_PLATFORM=offscreen`) with zero Qt installed on the host.

## Notes

* The `linux-cli` job builds the `linux-release-cli` preset on a runner with
  no Qt at all, guarding the graceful GUI skip.
* Submodules must be resolvable: CI checks out with `submodules: recursive`.
* Tag names must match `v<version>` (e.g. `v1.0`); the `v` prefix is stripped
  for artifact names and AppImage version metadata.
* `ci/ryty-gui.desktop`, `ci/ryty.png` and `ci/README.txt` feed the AppImage
  and the Linux zip; keep them next to the workflow.
