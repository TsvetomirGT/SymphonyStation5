// SymphonyStation5 - album and artist pictures as GL textures.
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "navi/tasks.hpp"
#include "subsonic/client.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace hui::gfx
{
class Renderer;
}

namespace navi
{

// get(id) returns the texture for a cover-art id, or 0 while it loads (the
// card then draws its placeholder) and requests it. Downloads and decoding
// happen on two workers, newest request first, so what is on screen now wins
// over what scrolled past; the texture upload happens on the UI thread in
// pump(). Least recently used textures are deleted past a budget.
class CoverCache
{
  public:
    explicit CoverCache(hui::gfx::Renderer &renderer);
    ~CoverCache();

    // Covers come from this server from now on; drops everything cached.
    void set_client(std::shared_ptr<const subsonic::Client> client);

    std::uint32_t get(const std::string &cover_id);
    // Call once per frame on the UI thread.
    void pump();

  private:
    struct Entry
    {
        std::uint32_t texture = 0;
        bool requested = false;
        bool failed = false;
        std::uint64_t last_used = 0;
    };
    void evict();
    void clear();

    hui::gfx::Renderer &renderer_;
    TaskPool pool_{2, true};
    std::shared_ptr<const subsonic::Client> client_;
    std::unordered_map<std::string, Entry> entries_;
    std::uint64_t frame_ = 0;
    std::uint64_t generation_ = 0; // bumps on set_client; stale results are dropped
    std::size_t textures_ = 0;
};

} // namespace navi
