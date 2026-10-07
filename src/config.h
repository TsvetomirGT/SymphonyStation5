#pragma once

#include <string>
#include <vector>

// Server credentials, stored as JSON:
//   { "server": "http://192.168.0.10:4533", "username": "...", "password": "..." }
// Entered on the login screen and saved so the next launch logs in by itself.
struct Config {
  std::string server;
  std::string username;
  std::string password;
};

// Where the login is kept, most preferred first.
//   native title:      /download0/navistation5/config.json (title storage),
//                      /app0/config.json (install folder, if writable),
//                      /download0/config.json (old location, read only)
//   desktop:           config.json in cwd
std::vector<std::string> ConfigPaths();

// Loads the first valid config in ConfigPaths(). On success *source is the
// path it came from; otherwise *error says why each place failed.
bool LoadSavedConfig(Config* out, std::string* source, std::string* error);

// Saves to every writable place in ConfigPaths(). True if at least one
// worked; *saved lists those, *error the failures.
bool SaveConfigEverywhere(const Config& config, std::vector<std::string>* saved,
                          std::string* error);

// Returns false and sets *error if the file is missing or malformed.
bool LoadConfig(const std::string& path, Config* out, std::string* error);

// Atomically writes `config` to `path` (temp file, fsync, rename).
bool SaveConfig(const std::string& path, const Config& config,
                std::string* error);

// Trims whitespace and trailing slashes, and adds "http://" when no scheme
// was typed ("192.168.0.10:4533" -> "http://192.168.0.10:4533").
std::string NormalizeServerUrl(const std::string& url);
