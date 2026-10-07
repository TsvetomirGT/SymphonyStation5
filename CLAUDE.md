# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

SymphonyStation5 is a Navidrome (OpenSubsonic API) music client for a **jailbroken PS5**, built as a **native PS5 title**: home-screen tile **PPSA17641**, installed by ShadowMountPlus from `/data/homebrew/PPSA17641/`. The UI uses **ps5-homebrew-ui** (OpenGL 4.6 through ps5-opengl) in its **Fresh** theme. The owner's main goal is **learning** the PS5 homebrew toolchain, so prefer explaining *why* over hiding details. The roadmap lives in `~/.claude/plans/i-want-to-learn-recursive-valiant.md`.

Development is **console-first**: the UI only runs on the PS5, because macOS has no OpenGL 4.5. The platform-independent core is unit-tested on the Mac.

## Commands

```sh
make test                          # macOS: build + run core unit tests (CMake/ctest, tests/core_tests.cpp)
make app                           # native title -> dist/PPSA17641/ (eboot.bin fSELF, sce_module/libc.prx, assets/)
make app-deploy PS5_HOST=<ip>      # build + FTP to /data/homebrew/PPSA17641 (ShadowMountPlus registers it)
make app-launch PS5_HOST=<ip>      # start the title via websrv /launch (replies 503 even on success)
make klog PS5_HOST=<ip>            # kernel log: our sys::log("[NAVI] ...") lines and crash reports
```

The console must be running the daemon payloads: ftpsrv (:2121), websrv (:8080) and klogsrv (:3232). They come from the pacbrew `ps5-payload-daemons.zip` release and are sent to elfldr with `socat -t 3 - TCP:<ip>:9021 < X.elf`. They don't survive a reboot.

Host prerequisites (macOS): `brew install llvm@18 coreutils findutils gnu-sed bash ninja ccache pkgconf curl`. The Makefile's `NATIVE_ENV` puts the GNU tools and LLVM 18 first on PATH. Pinned dependencies are fetched into `.deps/` (gitignored):
- payload SDK v0.42 and zlib (`tools/setup-native-dependencies.sh`);
- PacBrew v0.40.2, used for libcurl/OpenSSL;
- ps5-opengl SDK 1.0.0 (`tools/fetch-opengl-sdk.sh`);
- LLVM 18 x86_64 compiler-rt builtins, from Ubuntu's `libclang-rt-18-dev` package (`tools/fetch-compiler-rt.sh`), because Homebrew's clang has only the Darwin runtimes.

## Layout

- `src/navi/`: **our app**. `app.*` holds the Login screen (3 `ui::TextField`, a `PushButton`, and a `ui::Keyboard` driven by the controller) and the Artists screen (`ui::ListView`). `library.*` runs the Navidrome worker on a pthread with a 1 MiB stack, because console default stacks are too small for TLS. `system_keyboard.*` is the parked PS5 system keyboard (IME dialog, loaded with `sceKernelLoadStartModule` + `sceKernelDlsym`); on hardware it reported "Keyboard unavailable".
- `src/main.cpp`: platform loop modelled on the kit's main. It opens the EGL display at 1080p, the pad and audio, then each frame runs `app.update` → play cues → `app.compose` → `renderer.present` → `display.swap`.
- **Core, no UI, unit-tested on the Mac:** `src/config.*` (credentials JSON: `/download0/config.json` on PS5, falling back to `/app0/config.json`), `src/net/http.*` (blocking libcurl GET), `src/subsonic/client.*` (token auth = md5(password + salt)), `src/util/` (file, md5, utf).
- **Vendored from ps5-homebrew-ui** (GPL-3.0; commit in `tooling/UPSTREAM`, licences in `licenses/`), kept unmodified so they can be updated:
  - `src/gfx`, `src/ui` (widgets, themes, `ui/components/*`), `src/core` (input, tweens, save_file), `src/audio`, `src/platform/ps5` (EGL display, pad, audio out, `sys::log`), `src/runtime` (`app_heap.c`: wrapped malloc in a fixed mspace; `runtime_shims.c`), `src/third_party/stb`;
  - `assets/fonts` (baked `.huifont`) and `assets/audio/sfx`;
  - `tooling/` and `tools/`. The kit's own `docs/` and `AGENTS.md` are the reference for the component APIs.
- `src/native/`: our native-only libc gaps. `console_curl.c` (from ps5-native-app-boilerplate) covers getaddrinfo on sceNetResolver, `__wrap_fcntl`, the CA list and non-blocking sockets. `runtime.cpp` covers nl_langinfo, ___mb_cur_max, strcasestr and arc4random.
- `third_party/nlohmann/json.hpp`: used with `JSON_NOEXCEPTION`, since the native build has no exceptions. Use `parse(..., allow_exceptions=false)` and `dump(..., error_handler_t::replace)`.

`tools/build.sh` compiles **every** `.c/.cpp` under `src/` with `-DNAVI_NATIVE`, C++20, `-fno-exceptions -fno-rtti`. A new source file is picked up automatically, so keep desktop-only code out of `src/`.

## Local patches to vendored tools (re-apply when updating from upstream)

- `tools/build.sh` sorts the SDK stub list glibc-style. lld binds each import to the **first** stub defining it, and macOS glob order put `libkernel_stub_weak.so` (soname `libkernel_web.sprx`) before `libkernel.so`. That made `open()` a null import in the native title.
- `tools/build.sh` takes `APP_WRAP_SYMBOLS` (curl needs `--wrap=fcntl`).
- `tools/setup-native-dependencies.sh` builds zlib with `ARFLAGS=rc` (Darwin's `libtool -o` breaks `llvm-ar`).
- `tools/prepare-opengl.sh` falls back to `tools/fetch-compiler-rt.sh` for the builtins archive.

## Native title rules learned on hardware

- **Imports from modules a native title doesn't load resolve to NULL**, so calling one jumps to address 0. Seen with `libScePosixForWebKit` (getaddrinfo, arc4random, strcasestr, mkstemp, isatty) and `libSceKeyboard`/`libSceImeDialog`. Fix by defining the function in the app. After adding a library, check `llvm-readelf -d build/llvm-pie.elf | grep NEEDED`, and check for symbols that only `libScePosixForWebKit.so` provides.
- Duplicate-symbol link errors mean the kit runtime or the OpenGL group already provides the symbol; delete ours. This happened with `__assert`, `_Unwind_*`, emutls and `mkstemp`.
- `/app0` is the app image (assets at `/app0/assets`). For a ShadowMountPlus folder install it maps to `/data/homebrew/PPSA17641` and **is writable**. `/download0` is title-local storage and persists across launches. `/data` isn't visible from the sandbox. The login is saved to `/download0/navistation5/config.json` and to `/app0/config.json`; see `ConfigPaths()`.
- **`access()` returns EPERM in the native sandbox, even for files the app can open.** Never test whether a file exists with `access()`/`FileExists()` there; just `open()` it. This one check made it look as if the saved login had vanished.
- Debugging: `sys::log` goes to klog. A crash prints registers and a backtrace there; symbolize with `llvm-symbolizer --obj=build/llvm-pie.elf <addr - 0x400000 - 1>`.
- A new title ID takes ShadowMountPlus a few seconds to register; launching fails with `0x80940031` until then. ShadowMountPlus stages `param.json` and home-screen art once.
- End the app with `sceSystemServiceLoadExec("exit", NULL)`, never `exit()`. The kit's `sys::quit()` does this.
