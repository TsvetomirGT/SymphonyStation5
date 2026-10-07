#include "config.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstring>
#include <vector>

#include "util/file.h"

namespace {

std::string JsonString(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

// Creates the parent directory of `path` if it is missing.
void EnsureParent(const std::string& path) {
  size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) {
    return;
  }
  mkdir(path.substr(0, slash).c_str(), 0755);
}

}  // namespace

std::vector<std::string> ConfigPaths() {
#ifdef NAVI_NATIVE
  return {
      // The title's own storage, in a folder as ps5-homebrew-ui keeps its
      // settings (/download0/hui).
      "/download0/navistation5/config.json",
      // The install folder (/data/homebrew/PPSA17641 through
      // ShadowMountPlus): survives the title's storage being recreated, if
      // the sandbox lets the app write there.
      "/app0/config.json",
      // Where builds before 2026-10-07 saved it.
      "/download0/config.json",
  };
#else
  return {AbsPath("config.json")};
#endif
}

bool LoadSavedConfig(Config* out, std::string* source, std::string* error) {
  std::string reasons;
  for (const std::string& path : ConfigPaths()) {
    std::string why;
    if (LoadConfig(path, out, &why)) {
      *source = path;
      return true;
    }
    reasons += (reasons.empty() ? "" : "; ") + why;
  }
  *error = reasons;
  return false;
}

bool SaveConfigEverywhere(const Config& config, std::vector<std::string>* saved,
                          std::string* error) {
  std::string reasons;
  const std::vector<std::string> paths = ConfigPaths();
  // The last path is only read (an old location); save to the others.
  const size_t count = paths.size() > 1 ? paths.size() - 1 : paths.size();
  for (size_t i = 0; i < count; ++i) {
    std::string why;
    EnsureParent(paths[i]);
    if (SaveConfig(paths[i], config, &why)) {
      saved->push_back(paths[i]);
    } else {
      reasons += (reasons.empty() ? "" : "; ") + why;
    }
  }
  *error = reasons;
  return !saved->empty();
}

bool LoadConfig(const std::string& path, Config* out, std::string* error) {
  // No access() check first: the native title's sandbox refuses access()
  // with EPERM even for files it can open, so just try to read.
  std::vector<unsigned char> data;
  if (!ReadFile(path, &data)) {
    *error = path + ": " + std::strerror(errno);
    return false;
  }
  auto json = nlohmann::json::parse(data.begin(), data.end(), nullptr,
                                    /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.is_object()) {
    *error = path + " is not valid JSON";
    return false;
  }
  out->server = NormalizeServerUrl(JsonString(json, "server"));
  out->username = JsonString(json, "username");
  out->password = JsonString(json, "password");
  if (out->server.empty() || out->username.empty()) {
    *error = path + " needs \"server\" and \"username\"";
    return false;
  }
  return true;
}

bool SaveConfig(const std::string& path, const Config& config,
                std::string* error) {
  nlohmann::json json = {
      {"server", config.server},
      {"username", config.username},
      {"password", config.password},
  };
  // `replace`: invalid UTF-8 would otherwise throw, and with exceptions
  // disabled (native build) that aborts the app.
  std::string data =
      json.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) +
      "\n";

  std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    *error = tmp + ": " + std::strerror(errno);
    return false;
  }
  size_t done = 0;
  while (done < data.size()) {
    ssize_t n = write(fd, data.data() + done, data.size() - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      *error = tmp + ": write: " + std::strerror(errno);
      close(fd);
      unlink(tmp.c_str());
      return false;
    }
    done += static_cast<size_t>(n);
  }
  if (fsync(fd) != 0) {
    *error = tmp + ": fsync: " + std::strerror(errno);
    close(fd);
    unlink(tmp.c_str());
    return false;
  }
  close(fd);
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    // Some filesystems refuse to replace an existing file.
    unlink(path.c_str());
    if (rename(tmp.c_str(), path.c_str()) != 0) {
      *error = path + ": rename: " + std::strerror(errno);
      unlink(tmp.c_str());
      return false;
    }
  }
  return true;
}

std::string NormalizeServerUrl(const std::string& url) {
  size_t begin = url.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  size_t end = url.find_last_not_of(" \t\r\n");
  std::string out = url.substr(begin, end - begin + 1);
  while (!out.empty() && out.back() == '/') {
    out.pop_back();
  }
  if (!out.empty() && out.find("://") == std::string::npos) {
    out = "http://" + out;
  }
  return out;
}
