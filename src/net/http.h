// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

struct HttpResponse {
  long status = 0;    // HTTP status, 0 if the request never completed
  std::string body;
  std::string error;  // transport error (DNS, connect, TLS...), empty on success

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Call once at startup (before any threads issue requests).
void HttpGlobalInit();
void HttpGlobalCleanup();

// Blocking GET. Safe to call from worker threads (one curl handle per call).
HttpResponse HttpGet(const std::string& url, long timeout_seconds = 15);

// Percent-encode a query-string component.
std::string UrlEncode(const std::string& s);
