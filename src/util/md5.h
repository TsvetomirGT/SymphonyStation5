// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

// MD5 of `data`, as 32 lowercase hex chars. Only used for Subsonic token
// auth (token = md5(password + salt)); not for anything security-critical.
std::string Md5Hex(const std::string& data);
