// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navi/covers.hpp"

#include "gfx/renderer.hpp"
#include "net/http.h"
#include "platform/ps5/system.hpp"

#include <GL/glcorearb.h>

#include <algorithm>
#include <vector>

extern "C" {
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y,
                                     int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);
}

namespace navi
{

namespace
{
// Requested from the server already scaled; a grid cell is ~260 px and the
// Now Playing art ~480 px on the 1080p canvas.
constexpr int kCoverSize = 400;
// 400 x 400 RGBA is 640 KB; 160 textures stay under ~100 MB of GPU memory.
constexpr std::size_t kTextureBudget = 160;

struct Decoded
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};
} // namespace

CoverCache::CoverCache(hui::gfx::Renderer &renderer) : renderer_(renderer) {}

CoverCache::~CoverCache()
{
    clear();
}

void CoverCache::set_client(std::shared_ptr<const subsonic::Client> client)
{
    client_ = std::move(client);
    ++generation_;
    pool_.clear_pending();
    clear();
}

void CoverCache::clear()
{
    for (auto &[id, entry] : entries_)
    {
        if (entry.texture != 0)
        {
            GLuint name = entry.texture;
            glDeleteTextures(1, &name);
        }
    }
    entries_.clear();
    textures_ = 0;
}

std::uint32_t CoverCache::get(const std::string &cover_id)
{
    if (cover_id.empty() || !client_)
        return 0;
    Entry &entry = entries_[cover_id];
    entry.last_used = frame_;
    if (entry.texture != 0 || entry.requested || entry.failed)
        return entry.texture;

    entry.requested = true;
    auto decoded = std::make_shared<Decoded>();
    const std::string url = client_->CoverArtUrl(cover_id, kCoverSize);
    const std::uint64_t generation = generation_;
    pool_.post(
        [url, decoded]
        {
            HttpResponse response = HttpGet(url, 20);
            if (!response.ok())
                return;
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char *pixels = stbi_load_from_memory(
                reinterpret_cast<const unsigned char *>(response.body.data()),
                static_cast<int>(response.body.size()), &width, &height, &channels, 4);
            if (pixels == nullptr)
                return;
            decoded->width = width;
            decoded->height = height;
            decoded->rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
            stbi_image_free(pixels);
        },
        [this, cover_id, decoded, generation]
        {
            if (generation != generation_)
                return; // the server changed while this was loading
            auto it = entries_.find(cover_id);
            if (it == entries_.end())
                return;
            if (decoded->rgba.empty())
            {
                it->second.failed = true;
                return;
            }
            it->second.texture = renderer_.batch().create_texture(decoded->width, decoded->height,
                                                                  decoded->rgba.data());
            ++textures_;
        });
    return 0;
}

void CoverCache::pump()
{
    ++frame_;
    pool_.pump();
    if (textures_ > kTextureBudget)
        evict();
}

void CoverCache::evict()
{
    // Oldest first, never one drawn in the last frame or two.
    std::vector<std::pair<std::uint64_t, std::string>> loaded;
    for (const auto &[id, entry] : entries_)
    {
        if (entry.texture != 0 && entry.last_used + 2 < frame_)
            loaded.emplace_back(entry.last_used, id);
    }
    std::sort(loaded.begin(), loaded.end());
    std::size_t excess = textures_ - kTextureBudget;
    for (std::size_t i = 0; i < loaded.size() && excess > 0; ++i, --excess)
    {
        auto it = entries_.find(loaded[i].second);
        GLuint name = it->second.texture;
        glDeleteTextures(1, &name);
        entries_.erase(it);
        --textures_;
    }
}

} // namespace navi
