Ryty - PS5 executable porting tool (Linux x86_64)
==================================================

Contents
--------
  ryty                               CLI porting tool (standalone binary)
  Ryty-GUI-<version>-linux-x86_64.AppImage
                                     Qt6 GUI with the complete Qt6 runtime
                                     bundled (single file, no Qt install needed)

Quick start
-----------
  1. Make things executable (once):
       chmod +x ryty Ryty-GUI-*-linux-x86_64.AppImage

  2. Command line porting:
       ./ryty --to-intel input.elf output.elf          (Linux target)
       ./ryty --to-intel --windows input.elf out.exe   (Windows target)
       ./ryty --help                                   (full option list)

  3. GUI:
       ./Ryty-GUI-*-linux-x86_64.AppImage

Notes
-----
  * The AppImage carries its own Qt6 libraries and plugins; it runs on any
    mainstream x86_64 distribution (glibc 2.39+, e.g. Ubuntu 24.04, Fedora 40,
    Debian 13). On systems without libfuse2, run it with:
       ./Ryty-GUI-*-linux-x86_64.AppImage --appimage-extract-and-run
  * OpenGL is provided by your system's graphics drivers (standard AppImage
    behavior); the GUI needs working GL/EGL for rendering.
  * Headless / SSH usage (no display server):
       QT_QPA_PLATFORM=offscreen ./Ryty-GUI-*-linux-x86_64.AppImage
    (the offscreen platform plugin ships inside the AppImage).
  * Place ryty-gui next to the ryty binary; the GUI launches the CLI with the
    options you pick and streams its log.
