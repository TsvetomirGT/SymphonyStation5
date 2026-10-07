# SymphonyStation5 licence and third-party notices

SymphonyStation5 is Copyright (C) 2026 tsvetomirgt and is licensed under the
GNU General Public License, version 3 or (at your option) any later version.
The full text is in [`LICENSE`](LICENSE).

The project has to be GPL-3.0-or-later: it is built on, and statically links,
ps5-homebrew-ui, ps5-native-app-boilerplate and the ps5-opengl SDK, which are
all GPL-3.0-or-later. Every other component below uses a licence that is
compatible with GPLv3.

## Corresponding source

The release (`PPSA17641.zip`) contains object code. Its Corresponding Source
is this repository at the release tag, plus the pinned upstream sources listed
below; `make app` rebuilds the title from them. A release must publish the
source archive of the tag next to the binary.

## Components in the released title

These are compiled or linked into `eboot.bin`, or shipped in the title folder.
Licence texts are in [`licenses/`](licenses), which the build copies into
`dist/PPSA17641/licenses/`.

| Component | Version / source | Licence | Text |
| --- | --- | --- | --- |
| ps5-homebrew-ui (`src/gfx`, `src/ui`, `src/core`, `src/audio`, `src/platform`, `src/runtime`, `tooling/`, `tools/`, assets) | [blackbearreloaded/ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) @ `4bd9425` (`tooling/UPSTREAM`) | GPL-3.0-or-later, (C) 2026 BlackBearReloaded | `licenses/ps5-homebrew-ui-*` |
| ps5-native-app-boilerplate (`src/native/console_curl.c`, native tooling, `runtime/libc.prx`) | [blackbearreloaded/ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | GPL-3.0-or-later, (C) 2026 BlackBearReloaded | `licenses/ps5-native-app-boilerplate-*` |
| ps5-opengl SDK (with Mesa, OpenGNM PSBC) | 1.0.0, [blackbearreloaded/ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | GPL-3.0-or-later; Mesa MIT; others per SDK notices | `licenses/ps5-opengl/` (copied from the SDK at build time) |
| libcurl | 8.18.0 (PacBrew v0.40.2) | curl licence (MIT-style) | `licenses/curl-COPYING.txt` |
| OpenSSL (libssl, libcrypto) | 3.5.2 (PacBrew v0.40.2) | Apache-2.0 | `licenses/OpenSSL-LICENSE.txt` |
| zstd | 1.5.6 (PacBrew v0.40.2) | BSD-3-Clause (dual BSD/GPLv2) | `licenses/zstd-LICENSE.txt` |
| libpsl | 0.21.5 (PacBrew v0.40.2) | MIT; its built-in Public Suffix List data is MPL-2.0 | `licenses/libpsl-COPYING.txt` |
| zlib | 1.3.2 | zlib licence | `licenses/zlib-LICENSE.txt` |
| nlohmann/json (`third_party/nlohmann/json.hpp`) | 3.12.0 | MIT, (c) 2013-2025 Niels Lohmann | `licenses/nlohmann-json-LICENSE.MIT.txt` |
| minimp3 (`third_party/minimp3`) | lieff/minimp3 @ `ea99364` | CC0-1.0 | `licenses/minimp3-LICENSE.txt` |
| stb_image, stb_vorbis (`third_party/stb`, `src/third_party/stb`) | nothings/stb (see `UPSTREAM` files) | Public domain or MIT | `licenses/stb-LICENSE.txt` |
| Inter, Montserrat, Press Start 2P, Patrick Hand fonts (baked `.huifont`) | via ps5-homebrew-ui | SIL OFL 1.1 | `assets/fonts/*-LICENSE.txt` |
| DejaVu Sans Mono font (baked `.huifont`) | via ps5-homebrew-ui | Bitstream Vera licence | `assets/fonts/DejaVu-LICENSE.txt` |
| Sound effects (`assets/audio/sfx`) | from ProsperoPuzzles / ProsperoEden via ps5-homebrew-ui | GPL-3.0-or-later, (C) 2026 BlackBearReloaded | see ps5-homebrew-ui notices |

The ps5-homebrew-ui and ps5-native-app-boilerplate notices
(`licenses/*-THIRD_PARTY_NOTICES.md`) list further credits, including the
SharpProspero-derived FSELF tooling and Inigo Quilez's distance functions.

## Build-only tools (not distributed)

The PS5 payload SDK v0.42, LLVM/Clang 18 and its compiler-rt builtins
(Apache-2.0 WITH LLVM-exception), PacBrew and GoogleTest are fetched into
the ignored `.deps/` directory and keep their own licences. No Sony SDK file,
proprietary runtime module, encryption key or game file is included.

## Trademarks

SymphonyStation5 is an unofficial homebrew client. It is not affiliated with,
endorsed or sponsored by Sony Interactive Entertainment or the Navidrome
project. "PlayStation" and "PS5" are trademarks of Sony Interactive
Entertainment Inc.
