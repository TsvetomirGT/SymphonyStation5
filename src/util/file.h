#pragma once

#include <string>
#include <vector>

// Resolve a path against cwd. Inside the PS5 BigApp process (websrv hbldr),
// open()/fopen() with relative paths fail with EINVAL, so every file access
// must go through an absolute path.
std::string AbsPath(const std::string& path);

// Read a whole file (path may be relative; it is resolved with AbsPath).
// Logs errno to stdout on failure.
bool ReadFile(const std::string& path, std::vector<unsigned char>* out);

bool FileExists(const std::string& path);

// Path of a bundled asset (fonts etc.). Native PS5 titles read them from the
// read-only app image at /app0/assets; desktop and payload builds use
// assets/ relative to cwd.
std::string AssetPath(const std::string& name);
