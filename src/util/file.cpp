#include "util/file.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

std::string AbsPath(const std::string& path) {
  if (path.empty() || path[0] == '/') {
    return path;
  }
  char cwd[512];
  if (!getcwd(cwd, sizeof(cwd))) {
    return path;
  }
  return std::string(cwd) + "/" + path;
}

// POSIX open/read rather than stdio: fopen() also failed with EINVAL in the
// BigApp process, even with the path fixed up.
bool ReadFile(const std::string& path, std::vector<unsigned char>* out) {
  std::string abs = AbsPath(path);
  int fd = open(abs.c_str(), O_RDONLY);
  if (fd < 0) {
    const int saved = errno;  // callers report errno; printf may change it
    std::printf("open(%s): %s\n", abs.c_str(), std::strerror(saved));
    errno = saved;
    return false;
  }
  unsigned char buf[64 * 1024];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    out->insert(out->end(), buf, buf + n);
  }
  if (n < 0) {
    std::printf("read(%s): %s\n", abs.c_str(), std::strerror(errno));
  }
  close(fd);
  return n == 0;
}

// Not usable in the native PS5 title: its sandbox refuses access() with EPERM
// even for files it can open. Try to open the file instead.
bool FileExists(const std::string& path) {
  return access(AbsPath(path).c_str(), R_OK) == 0;
}

std::string AssetPath(const std::string& name) {
#ifdef NAVI_NATIVE
  return "/app0/assets/" + name;
#else
  return "assets/" + name;
#endif
}
