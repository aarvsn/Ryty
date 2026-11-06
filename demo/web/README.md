# Ryty Web - PS4 / PS5 Web-Based Executable Porting Studio

`demo/web` is an interactive browser application for inspecting, analyzing, and porting PlayStation 4 and PlayStation 5 executables (`eboot.elf`, PRX dynamic modules) using the Ryty v2.0.0 relinker engine specifications.

Inspired by AnyPS5 `demo/web`, this Web application provides:

* **Client-Side ELF Parsing**: Full parsing of ELF64 x86-64 header structures, `PT_LOAD`, `PT_DYNAMIC`, section tables and symbol tables directly in browser memory without sending files to external servers.
* **Auto Console Detection**: Identifies whether an input executable targets PlayStation 4 or PlayStation 5 based on system PRX module signatures (`libkernel.sprx` vs `libkernel_sys.prx`).
* **NID & Symbol Inspection**: Resolves `DT_NEEDED` dynamic library dependencies, NID functions, and exports bounded strictly by `DT_STRSZ`.
* **Lowering & Target Options**: Configure target OS (Linux, Windows, macOS with Metal API bridge), instruction lowering stubs (SHA-NI and AMD-only x86-64 lowering), NID reachability levels, and RytyNet online service toggles.
* **Porting Reports**: Generates downloadable JSON porting reports and converted binary metadata representations.

## New in 2.0.0

* **Dark / light theme**: toggle in the header, stored locally and applied on the next visit (defaults to the OS preference).
* **Section header viewer**: every section is listed with type, flags, offsets and sizes; unknown SCE section types are decoded.
* **Hex viewer**: inspect any segment or section as a canonical hex + ASCII dump, with byte-range selection and "jump to offset". Every program/section table row has a `hex` shortcut.
* **Live NID filter**: filter the symbol table by substring or `/regex/`, with a live match counter.
* **Compare tab**: diff a second ELF against the loaded one - console target, entry point, needed libraries and the exact NID set (added / removed symbols) at a glance.
* **Offline PWA**: `manifest.webmanifest` + `sw.js` precache the whole studio; after the first visit it works with the network fully disconnected. An explicit "Prefer Offline Cache" checkbox documents the behaviour; the service worker registers automatically on `https://`/`localhost`.
* **Copy report** button (clipboard API with a selection fallback) and an honest **Export** that downloads the analysis metadata JSON plus the exact inspected binary bytes.
* DT_SYMTAB sizing now uses `DT_SYMENT`/`DT_SYMTAB` bounds and scans up to 4096 symbols (previously a fixed 200).

## Usage

Simply open `index.html` in any modern web browser (Google Chrome, Mozilla Firefox, Apple Safari, Microsoft Edge). No server-side setup or compilation required.

```sh
# Optionally serve locally using Python (also enables the PWA service worker):
python3 -m http.server 8080 --directory demo/web
# Then open http://localhost:8080 in your browser
```

Everything runs client side: the loaded executable never leaves the browser.
