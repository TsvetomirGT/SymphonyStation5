// Small libc gaps a native PS5 title has to fill itself.
//
// PacBrew's libcurl/OpenSSL were built for payloads, which run with the
// payload SDK's own libc. A native title only gets Sony's libSceLibcInternal
// and libkernel, so these were undefined at link time, or bound to
// libScePosixForWebKit (not loaded in a native title, so a null import).
// console_curl.c covers networking; src/runtime/runtime_shims.c (from
// ps5-homebrew-ui) covers mkstemp/isatty/popen; libunwind and compiler-rt
// (linked by tools/prepare-opengl.sh) cover unwinding and emulated TLS.
//
// Compiled only into the native title (NAVI_NATIVE).

#ifdef NAVI_NATIVE

#include <langinfo.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

extern "C" {

// ---- small libc gaps ----------------------------------------------------

// libiconv/SDL ask for the locale's codeset; the console has no locales.
char* nl_langinfo(nl_item item) {
  return const_cast<char*>(item == CODESET ? "UTF-8" : "");
}

// FreeBSD's MB_CUR_MAX expands to a call of this. "C" locale: 1 byte.
int ___mb_cur_max(void) { return 1; }

// The SDK stubs only export these from libScePosixForWebKit, which a native
// title does not load (the import would be null), so define them here.

char* strcasestr(const char* haystack, const char* needle) {
  size_t n = strlen(needle);
  for (; *haystack; haystack++) {
    if (strncasecmp(haystack, needle, n) == 0) {
      return const_cast<char*>(haystack);
    }
  }
  return n == 0 ? const_cast<char*>(haystack) : nullptr;
}

// Not cryptographic-grade when /dev/urandom is unavailable; TLS randomness
// comes from OpenSSL, not from here.
uint32_t arc4random(void) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  static uint64_t state = 0;
  pthread_mutex_lock(&mutex);
  if (state == 0) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f || fread(&state, sizeof(state), 1, f) != 1) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      state = static_cast<uint64_t>(ts.tv_nsec) ^
              (static_cast<uint64_t>(ts.tv_sec) << 32) ^
              reinterpret_cast<uintptr_t>(&state);
    }
    if (f) {
      fclose(f);
    }
    state |= 1;
  }
  // xorshift64*
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  uint32_t result = static_cast<uint32_t>((state * 0x2545F4914F6CDD1DULL) >> 32);
  pthread_mutex_unlock(&mutex);
  return result;
}

}  // extern "C"

#endif  // NAVI_NATIVE
