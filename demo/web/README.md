# Ryty Web - PS4 / PS5 Web-Based Executable Porting Studio

`demo/web` is an interactive browser application for inspecting, analyzing, and porting PlayStation 4 and PlayStation 5 executables (`eboot.elf`, PRX dynamic modules) using the Ryty v1.1.1 relinker engine specifications.

Inspired by AnyPS5 `demo/web`, this Web application provides:

* **Client-Side ELF Parsing**: Full parsing of ELF64 x86-64 header structures, `PT_LOAD`, `PT_DYNAMIC`, and symbol tables directly in browser memory without sending files to external servers.
* **Auto Console Detection**: Identifies whether an input executable targets PlayStation 4 or PlayStation 5 based on system PRX module signatures (`libkernel.sprx` vs `libkernel_sys.prx`).
* **NID & Symbol Inspection**: Resolves `DT_NEEDED` dynamic library dependencies, NID functions, and exports bounded strictly by `DT_STRSZ`.
* **Lowering & Target Options**: Configure target OS (Linux, Windows, macOS with Metal API bridge), instruction lowering stubs (SHA-NI and AMD-only x86-64 lowering), NID reachability levels, and RytyNet online service toggles.
* **Porting Reports**: Generates downloadable JSON porting reports and converted binary metadata representations.

## Usage

Simply open `index.html` in any modern web browser (Google Chrome, Mozilla Firefox, Apple Safari, Microsoft Edge). No server-side setup or compilation required.

```sh
# Optionally serve locally using Python:
python3 -m http.server 8080 --directory demo/web
# Then open http://localhost:8080 in your browser
```
