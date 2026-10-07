# SymphonyStation5 - build entry points.
#
#   make test               desktop unit tests for the core (macOS, CMake + ctest)
#   make app                native PS5 title -> dist/<TITLE_ID>/
#   make app-deploy         build + FTP to /data/homebrew/<TITLE_ID> ($PS5_HOST)
#   make app-launch         start the title via websrv /launch
#   make klog               stream the console's kernel log (klogsrv :3232)
#
# The UI (ps5-homebrew-ui, OpenGL 4.6 through ps5-opengl) only runs on the
# console; macOS has no OpenGL 4.5. The platform-independent core (Navidrome
# client, HTTP, config) is unit-tested on the Mac.

PS5_HOST        ?= ps5
PS5_FTP_PORT    ?= 2121

TITLE_ID        := $(shell python3 -c 'import json;print(json.load(open("sce_sys/param.json"))["titleId"])')

# The vendored scripts expect GNU tools (sha256sum --strict, find -printf,
# bash 5, ...) and clang-18; put Homebrew's GNU tools and LLVM 18 first.
NATIVE_PATH     := /opt/homebrew/bin:/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:/opt/homebrew/opt/gnu-sed/libexec/gnubin:/opt/homebrew/opt/llvm@18/bin:$(PATH)

OPENGL_SDK      := .deps/ps5-opengl/current

NATIVE_ENV      := PATH="$(NATIVE_PATH)" \
	APP_DEFINITIONS="NAVI_NATIVE JSON_NOEXCEPTION GL_GLEXT_PROTOTYPES=1" \
	APP_INCLUDE_PATHS="src third_party $(OPENGL_SDK)/include" \
	APP_STATIC_ARCHIVES=".deps/ps5-opengl/libps5opengl-group.a" \
	APP_IMPORT_STUBS="$(OPENGL_SDK)/lib/libSceAgc.so $(OPENGL_SDK)/lib/libSceAgcDriver.so" \
	PACBREW_PACKAGES="libcurl" \
	APP_WRAP_SYMBOLS="fcntl"

.PHONY: all test desktop opengl app app-deploy app-undeploy app-launch klog clean

all: app

desktop:
	cmake -S . -B build/desktop -DCMAKE_BUILD_TYPE=Debug
	cmake --build build/desktop -j

test: desktop
	ctest --test-dir build/desktop --output-on-failure

# Fetches the pinned ps5-opengl SDK and writes its link group.
opengl:
	$(NATIVE_ENV) bash tools/prepare-opengl.sh

app: opengl
	$(NATIVE_ENV) bash tools/build.sh Folder

# Builds (via `make app`) and uploads dist/<TITLE_ID> to /data/homebrew.
app-deploy:
	$(NATIVE_ENV) PS5_HOST=$(PS5_HOST) FTP_PORT=$(PS5_FTP_PORT) bash tools/deploy.sh

app-undeploy:
	$(NATIVE_ENV) PS5_HOST=$(PS5_HOST) FTP_PORT=$(PS5_FTP_PORT) bash tools/deploy.sh undeploy

# websrv's /launch starts a registered title. It replies 503 when the launch
# outlives its wait, even though the title starts; check `make klog`.
app-launch:
	-curl -s -m 25 -o /dev/null "http://$(PS5_HOST):8080/launch?titleId=$(TITLE_ID)"

# Native titles log to the kernel log; needs klogsrv (port 3232) running.
klog:
	nc $(PS5_HOST) 3232

clean:
	rm -rf build dist
