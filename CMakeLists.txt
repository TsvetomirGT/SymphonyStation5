cmake_minimum_required(VERSION 3.16)

# Desktop (macOS) build of the platform-independent core and its unit tests.
# The app itself is a native PS5 title built by tools/build.sh (`make app`);
# its UI needs OpenGL 4.5+, which macOS lacks.

project(navistation5 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)

add_library(core STATIC
  src/config.cpp
  src/net/http.cpp
  src/subsonic/client.cpp
  src/util/file.cpp
  src/util/md5.cpp
  src/util/utf.cpp
)
target_include_directories(core PUBLIC src third_party)
target_compile_options(core PRIVATE -Wall -Wextra)
target_link_libraries(core PUBLIC CURL::libcurl)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE core)
add_test(NAME core_tests COMMAND core_tests)
