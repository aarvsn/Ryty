# Project technical debt

### Build

- Building on Windows requires a specific version of mingw - MinGW-w64 GCC 15.2.0 (`winlibs-gcc15`, `x86_64-ucrt-posix-seh`)
- Even compiled prx libraries on Windows require nearby (static linking of these dependencies causes conflicts):
  - libgcc_s_seh-1.dll
  - libstdc++-6.dll
  - libwinpthread-1.dll

### Silent stubs

Throughout the project, every function at every stage either **does exactly what it's supposed to or throws an exception**. Everywhere... except:
- [libSceSaveDataDialog.native](../../core/libs/prx/libSceSaveDataDialog.native/Export.cpp)
- [libSceCommonDialog](../../core/libs/prx/libSceCommonDialog/Export.cpp)
- [libSceHmd](../../core/libs/prx/libSceHmd/Export.cpp) implements only the disconnected-headset path: initialization succeeds, device queries report `NotDetected`, and opening a device returns `DeviceDisconnected`. Headset support, tracking, rendering and additional HMD exports are not implemented; SDK-level ABI compatibility and in-game behaviour remain unverified.
- [libSceNpCommerce](../../core/libs/prx/libSceNpCommerce/Export.cpp) - the PS Store icon show/hide calls do nothing
- [libScePlayerInvitationDialog](../../core/libs/prx/libScePlayerInvitationDialog/libScePlayerInvitationDialog.cpp) simulates dialog completion without displaying UI or sending invitations; parameter ABI remains unverified.
- The shader recompiler [skips baryctric coordinates](../../core/shader/recompiler/Recompiler.cpp) (is not even passed to SpirvTargetOptions at row 212).

### Unknown function info

- [AMPR WaitOnAddress / WaitOnCounter](../../core/libs/prx/libkernel/Apr/src/Apr.cpp) (libSceAmpr) - compare encoding assumed to be the WAIT_REG_MEM one (0 always, 1 <, 2 <=, 3 ==, 4 !=, 5 >=, 6 >)
- [sceAgcSetSubmitMode](../../core/libs/prx/libSceAgc/Misc/src/Suspend.cpp) (libSceAgc) - mode values unknown; only 0 is accepted
- [zARR5aCmkoY](../../core/libs/prx/libSceAgc/DcbFlow/src/Control.cpp) (libSceAgc) - unknown name, signature
- [qj7QZpgr9Uw](../../core/libs/prx/libSceAgc/DcbState/src/ContextState.cpp) (libSceAgc) - unknown name
- [fd5Bp5tGTgo](../../core/libs/prx/libSceAgc/Misc/src/ShaderFusion.cpp) (libSceAgc) - unknown name
- [dolOmWH+huQ](../../core/libs/prx/libSceAgc/Misc/src/ShaderFusion.cpp) (libSceAgc) - unknown name
- [V++UgBtQhn0](../../core/libs/prx/libSceAgc/Misc/src/PacketInfo.cpp) (libSceAgc) - unknown name
- [gQkqkLttcpw](../../core/libs/prx/libSceAgc/Acb/src/Control.cpp) (libSceAgc) - unknown name, signature
- [sceKernelInternalMemoryGetModuleSegmentInfo](../../core/libs/prx/libkernel/Module/src/Module.cpp) (libkernel) - unknown signature
- [sceLibcInternalBacktraceForGame](../../core/libs/prx/libc/src/HeapDiagnostics.cpp) (libSceLibcInternal, implemented in libc) - unknown signature
- [sceLibcInternalHeapErrorReportForGame](../../core/libs/prx/libc/src/HeapDiagnostics.cpp) (libSceLibcInternal, implemented in libc) - unknown signature
- [__progname](../../core/libs/prx/libkernel/System/src/Process.cpp) (libkernel) - unknown data export
- [sceSslClose](../../core/libs/prx/libSceSsl/Export.cpp) (libSceSsl) - unknown signature
- [sceSslGetSerialNumber](../../core/libs/prx/libSceSsl/Export.cpp) (libSceSsl) - unknown signature

### Functional

- [Shader recompilation](../../core/shader/recompiler/Recompiler.cpp) currently occurs right before it was transferred to Vulkan with caching, but should be moved to the [relinker](../../core/relinker/main.cpp) stage. For this purpose, [shader/recompiler](../../core/shader/recompiler) was written completely independently from [libs/prx](../../core/libs/prx).
- The executable file that [relinker](../../core/relinker/elfpatcher/src/windows/WindowsPeWriter.cpp) generates opens the console when launched, which is inconvenient for playability.
- [libSceJpegEnc](../../core/libs/prx/libSceJpegEnc/Export.cpp) explicitly returns `SCE_JPEG_ERROR_UNSUPPORTED_SAMPLING` for 4:2:2 sampling requests and grayscale requests because the [JPEG encoder](../../core/Decoder/Jpeg/src/Jpeg.cpp) (stb) only produces 3-component 4:2:0 and 4:4:4 images.
- [Relinker](../../core/relinker/elfpatcher/src) doesn't add an icon to the generated executable. This should be done without adding dependencies (only standard).
- The length-changing path of the [instruction rewriter](../../core/relinker/codegen/src/x86/X64InstructionRewriter.cpp) is not used by the [converter](../../core/relinker/codegen/src/Amd64OnlyConverter.cpp): it does not adjust VEX/0F38/0F3A RIP-relative operands, data-to-code references (relocations, FDEs, jump tables) or segment sizes, so every substitution keeps the instruction length.
- The Linux placement of the `--to-intel` stubs in the [ELF patcher](../../core/relinker/elfpatcher/src/linux/LinuxElfPatcher.cpp) is covered only by a synthetic test.
- The libc [SSE4a trap emulation](../../core/libs/prx/libc/src/specifics/windows/Sse4aEmulation.hpp) on Windows is superseded by `--to-intel` and remains only until the relinked title has been verified without it.
- `--to-intel` guest module trampolines are covered only by a synthetic relinker test; no game title has been verified with them on Linux or Windows.
- [sceKeyboardGetKey2Char](../../core/libs/prx/libSceKeyboard/src/keyboard_impl.cpp) (libSceKeyboard) translates only the 101-key (US) arrangement and returns `KEYBOARD_ERROR_INVALID_ARG` for the 106-key (Japanese) layout instead of translating it; Ctrl and Alt do not change the character.
- [libSceAudiodec](../../core/libs/prx/libSceAudiodec/Export.cpp) returns `SCE_AUDIODEC_ERROR_INVALID_WORD_LENGTH` (`0x807F0010`) for the 24-bit PCM word size (`iBwPcm` 0): its sample layout is unknown. ATRAC9 decoding is covered only by configuration and error tests, as no ATRAC9 encoder is available for a fixture.
- [RytyNet](../../core/libs/prx/libRytyNet/src/RytyNetBridge.cpp) (1.0.7, experimental) performs plain HTTP/1.1 over TCP only: no TLS (https requests fail with the network error and a log line), no keep-alive, no proxies, and the `libSceNpManager` signed-in presence is local (`RytyPlayer`) with nothing behind it. The non-blocking `sceHttp*Epoll` API stays offline even with the bridge on.
- The macOS writer's guest `sce_module` PRXs are emitted as Linux ELF shared objects and loaded by the runtime's own loader; dyld load commands for them are deliberately not emitted (a `LC_LOAD_DYLIB` at an ELF file would abort launch), so a macOS port still needs its `libs/` populated like the Linux one. Mach-O guest modules would need a `GuestModuleWriter::WriteMachO` backend.
- The macOS ad-hoc code signature covers the classic CodeDirectory only (no requirements, entitlements or CMS slot); `codesign --force --deep -s -` remains the fallback if a hardened-runtime host refuses it.
