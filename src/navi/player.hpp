// SymphonyStation5 - streams songs from Navidrome into the kit's audio mixer.
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/mixer.hpp"
#include "navi/tasks.hpp"
#include "subsonic/client.h"

#include <memory>
#include <string>
#include <vector>

namespace navi
{

// Plays a queue of songs. Each song is fetched with Subsonic's stream
// endpoint (transcoded to MP3 by the server), decoded with minimp3 on a
// thread of its own, resampled to the mixer's 48 kHz and written into a
// StreamRing that the mixer plays on its music bus (stream slot 0).
//
// Pause takes the ring away from the mixer (the decoder simply waits while
// the ring is full); seek restarts the stream at the new position with
// timeOffset. Plays are reported with scrobble, which feeds Navidrome's
// "recently played".
class Player
{
  public:
    // Attaches to the mixer: construct before the audio thread starts.
    Player(hui::audio::Mixer &mixer, TaskPool &api);
    ~Player();

    void set_client(std::shared_ptr<const subsonic::Client> client);

    void play_queue(std::vector<subsonic::Song> songs, int index);
    void play();
    void pause();
    void next();
    void previous(); // restarts the song after its first few seconds
    void seek(float seconds);
    void set_volume(float level);

    // UI thread, once per frame: advances the queue, reports plays.
    void update(float dt);

    bool has_song() const
    {
        return index_ >= 0 && index_ < static_cast<int>(queue_.size());
    }
    const subsonic::Song *current() const
    {
        return has_song() ? &queue_[static_cast<std::size_t>(index_)] : nullptr;
    }
    const std::vector<subsonic::Song> &queue() const
    {
        return queue_;
    }
    int index() const
    {
        return index_;
    }
    bool playing() const
    {
        return playing_;
    }
    // Starts waiting for data (a new song or a seek) until audio arrives.
    bool buffering() const;
    float position() const;
    float duration() const;
    const std::string &error() const
    {
        return error_;
    }

    struct Stream; // implementation detail, shared with the decoder thread

  private:
    void start_stream(int offset_seconds);
    void stop_stream();
    void attach_current();

    hui::audio::Mixer &mixer_;
    TaskPool &api_;
    std::shared_ptr<const subsonic::Client> client_;
    std::vector<subsonic::Song> queue_;
    int index_ = -1;
    bool playing_ = false;
    std::shared_ptr<Stream> stream_;
    // Rings the mixer may still be reading for a few grains after a swap.
    std::vector<std::shared_ptr<Stream>> retired_;
    float retire_timer_ = 0.0f;
    bool scrobbled_ = false;
    std::string error_;
};

} // namespace navi
