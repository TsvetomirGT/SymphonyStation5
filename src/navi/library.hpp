// SymphonyStation5 - talks to the Navidrome server off the UI thread.
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "subsonic/client.h"

namespace navi
{

// Runs one request at a time (ping + getArtists) on a worker thread; the UI
// polls take() once per frame.
class Library
{
  public:
    struct Result
    {
        Config config; // what was used
        bool ok = false;
        std::string error;
        std::vector<subsonic::Artist> artists;
    };

    Library();
    ~Library();

    bool busy() const;
    // False if a request is already running.
    bool start(const Config &config);
    // True once per finished request.
    bool take(Result *out);

    struct Shared; // implementation detail, shared with the worker thread

  private:
    std::shared_ptr<Shared> shared_;
};

} // namespace navi
