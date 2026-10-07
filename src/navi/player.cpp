#include "navi/player.hpp"

#include "audio/stream_ring.hpp"
#include "platform/ps5/system.hpp"

#include <curl/curl.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#ifdef NAVI_NATIVE
#include "native/console_curl.h"
#endif

// minimp3's API (implementation in src/third_party/navi_minimp3.c).
extern "C" {
#include "minimp3/minimp3.h"
}

namespace navi
{

namespace
{

constexpr std::size_t kSlot = 0;                 // mixer stream slot
constexpr int kOutRate = hui::audio::kSampleRate; // 48 kHz
constexpr std::size_t kRingFrames = 48000 * 8;   // ~8 s of audio buffered
constexpr size_t kDecoderStack = 1024 * 1024;

} // namespace

struct Player::Stream
{
    hui::audio::StreamRing ring{kRingFrames};
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::atomic<std::uint64_t> frames_written{0};
    int offset_seconds = 0;
    std::string url;
    std::mutex mu;
    std::string error;

    // Decoder state (decoder thread only).
    mp3dec_t decoder;
    std::vector<unsigned char> mp3;
    std::vector<float> input; // decoded stereo frames not yet resampled
    double phase = 0.0;       // resampler position within `input`
    int in_rate = 0;
};

namespace
{

// Waits until the ring has room for `frames`, or the stream is stopped.
bool wait_for_room(Player::Stream &s, std::size_t frames)
{
    while (s.ring.space() < frames)
    {
        if (s.stop.load())
            return false;
        usleep(5000);
    }
    return !s.stop.load();
}

// Linear-interpolation resampler from in_rate to 48 kHz. Keeps the last
// input frame so interpolation continues across chunks.
bool resample_and_write(Player::Stream &s)
{
    const std::size_t in_frames = s.input.size() / 2;
    if (in_frames < 2)
        return true;
    const double step = static_cast<double>(s.in_rate) / kOutRate;
    float out[2048 * 2];
    std::size_t produced = 0;
    while (s.phase + 1.0 < static_cast<double>(in_frames))
    {
        const std::size_t i = static_cast<std::size_t>(s.phase);
        const float t = static_cast<float>(s.phase - static_cast<double>(i));
        out[produced * 2] = s.input[i * 2] + (s.input[i * 2 + 2] - s.input[i * 2]) * t;
        out[produced * 2 + 1] =
            s.input[i * 2 + 1] + (s.input[i * 2 + 3] - s.input[i * 2 + 1]) * t;
        ++produced;
        s.phase += step;
        if (produced == 2048)
        {
            if (!wait_for_room(s, produced))
                return false;
            s.ring.write(out, produced);
            s.frames_written.fetch_add(produced);
            produced = 0;
        }
    }
    if (produced > 0)
    {
        if (!wait_for_room(s, produced))
            return false;
        s.ring.write(out, produced);
        s.frames_written.fetch_add(produced);
    }
    // Drop the frames fully used; keep the one the next output starts from.
    const std::size_t used = static_cast<std::size_t>(s.phase);
    s.input.erase(s.input.begin(), s.input.begin() + static_cast<std::ptrdiff_t>(used * 2));
    s.phase -= static_cast<double>(used);
    return true;
}

// Decodes every whole MP3 frame in the buffer. With `flush`, also the tail.
bool decode_available(Player::Stream &s, bool flush)
{
    std::size_t offset = 0;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    // minimp3 wants a few frames of look-ahead to sync reliably.
    const std::size_t keep = flush ? 0 : 16 * 1024;
    while (s.mp3.size() - offset > keep)
    {
        mp3dec_frame_info_t info;
        const int samples = mp3dec_decode_frame(&s.decoder, s.mp3.data() + offset,
                                                static_cast<int>(s.mp3.size() - offset), pcm, &info);
        if (info.frame_bytes == 0)
            break; // needs more data
        offset += static_cast<std::size_t>(info.frame_bytes);
        if (samples == 0)
            continue; // skipped ID3 or garbage
        if (s.in_rate == 0)
            s.in_rate = info.hz;
        for (int i = 0; i < samples; ++i)
        {
            const float left = pcm[i * info.channels] / 32768.0f;
            const float right = info.channels > 1 ? pcm[i * info.channels + 1] / 32768.0f : left;
            s.input.push_back(left);
            s.input.push_back(right);
        }
        if (!resample_and_write(s))
            return false;
    }
    s.mp3.erase(s.mp3.begin(), s.mp3.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

size_t on_data(char *data, size_t size, size_t count, void *user)
{
    auto &s = *static_cast<Player::Stream *>(user);
    if (s.stop.load())
        return 0; // aborts the transfer
    s.mp3.insert(s.mp3.end(), data, data + size * count);
    if (!decode_available(s, false))
        return 0;
    return size * count;
}

void *decoder_main(void *arg)
{
    std::unique_ptr<std::shared_ptr<Player::Stream>> holder(
        static_cast<std::shared_ptr<Player::Stream> *>(arg));
    Player::Stream &s = **holder;
    mp3dec_init(&s.decoder);

    CURL *curl = curl_easy_init();
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, s.url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // No total timeout for a song; give up on a stalled connection instead.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SymphonyStation5/0.1");
#ifdef NAVI_NATIVE
    console_curl_setup(curl);
#endif
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (!s.stop.load())
    {
        if (rc == CURLE_OK && status >= 200 && status < 300)
        {
            decode_available(s, true);
            // Push out the very last resampler input.
            if (!s.input.empty() && s.in_rate != 0)
            {
                s.input.push_back(s.input[s.input.size() - 2]);
                s.input.push_back(s.input[s.input.size() - 2]);
                resample_and_write(s);
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(s.mu);
            s.error = rc != CURLE_OK ? (errbuf[0] ? errbuf : curl_easy_strerror(rc))
                                     : "HTTP " + std::to_string(status);
            hui::sys::log("[NAVI] stream failed: %s", s.error.c_str());
        }
    }
    s.finished.store(true);
    return nullptr;
}

} // namespace

Player::Player(hui::audio::Mixer &mixer, TaskPool &api) : mixer_(mixer), api_(api)
{
    mixer_.attach_stream(kSlot, nullptr);
    mixer_.set_stream_gain(kSlot, 1.0f, 0.0f);
}

Player::~Player()
{
    stop_stream();
}

void Player::set_client(std::shared_ptr<const subsonic::Client> client)
{
    client_ = std::move(client);
}

void Player::stop_stream()
{
    if (!stream_)
        return;
    stream_->stop.store(true);
    mixer_.swap_stream(kSlot, nullptr);
    // The mixer may read the old ring for a few more grains: keep it alive.
    retired_.push_back(std::move(stream_));
    retire_timer_ = 1.0f;
}

void Player::start_stream(int offset_seconds)
{
    stop_stream();
    error_.clear();
    scrobbled_ = scrobbled_ && offset_seconds > 0;
    const subsonic::Song *song = current();
    if (!song || !client_)
        return;
    auto stream = std::make_shared<Stream>();
    stream->offset_seconds = offset_seconds;
    stream->url = client_->StreamUrl(song->id, offset_seconds);
    stream_ = stream;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kDecoderStack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    auto *holder = new std::shared_ptr<Stream>(stream);
    if (pthread_create(&thread, &attr, decoder_main, holder) != 0)
    {
        delete holder;
        error_ = "could not start the decoder";
        stream_.reset();
    }
    pthread_attr_destroy(&attr);
    attach_current();

    if (offset_seconds == 0)
    {
        // Tell the server what is playing ("now playing").
        auto client = client_;
        const std::string id = song->id;
        api_.post([client, id] {
            std::string error;
            client->Scrobble(id, false, &error);
        });
    }
}

void Player::attach_current()
{
    mixer_.swap_stream(kSlot, playing_ && stream_ ? &stream_->ring : nullptr);
}

void Player::play_queue(std::vector<subsonic::Song> songs, int index)
{
    queue_ = std::move(songs);
    index_ = index;
    playing_ = true;
    scrobbled_ = false;
    start_stream(0);
}

void Player::play()
{
    if (!has_song())
        return;
    playing_ = true;
    if (!stream_ || (stream_->finished.load() && stream_->ring.available() == 0))
        start_stream(0);
    else
        attach_current();
}

void Player::pause()
{
    playing_ = false;
    attach_current();
}

void Player::next()
{
    if (index_ + 1 >= static_cast<int>(queue_.size()))
    {
        // End of the queue: stop on the last song.
        stop_stream();
        playing_ = false;
        return;
    }
    ++index_;
    scrobbled_ = false;
    start_stream(0);
}

void Player::previous()
{
    if (position() > 3.0f || index_ <= 0)
    {
        seek(0.0f);
        return;
    }
    --index_;
    scrobbled_ = false;
    start_stream(0);
}

void Player::seek(float seconds)
{
    if (!has_song())
        return;
    if (seconds < 0.0f)
        seconds = 0.0f;
    start_stream(static_cast<int>(seconds));
}

void Player::set_volume(float level)
{
    mixer_.set_bus_gain(hui::audio::Bus::music, level);
}

bool Player::buffering() const
{
    return playing_ && stream_ && !stream_->finished.load() && stream_->ring.available() == 0;
}

float Player::position() const
{
    if (!stream_)
        return 0.0f;
    const std::uint64_t written = stream_->frames_written.load();
    const std::size_t buffered = stream_->ring.available();
    const std::uint64_t played = written > buffered ? written - buffered : 0;
    return static_cast<float>(stream_->offset_seconds) +
           static_cast<float>(played) / static_cast<float>(kOutRate);
}

float Player::duration() const
{
    const subsonic::Song *song = current();
    return song ? static_cast<float>(song->duration) : 0.0f;
}

void Player::update(float dt)
{
    if (retire_timer_ > 0.0f)
    {
        retire_timer_ -= dt;
        if (retire_timer_ <= 0.0f)
            retired_.clear();
    }
    if (!stream_)
        return;
    {
        std::lock_guard<std::mutex> lock(stream_->mu);
        if (!stream_->error.empty() && error_.empty())
            error_ = stream_->error;
    }
    // A play counts once half the song (or four minutes) has been heard.
    if (!scrobbled_ && playing_ && duration() > 0.0f &&
        (position() >= duration() * 0.5f || position() >= 240.0f))
    {
        scrobbled_ = true;
        auto client = client_;
        const std::string id = current()->id;
        api_.post([client, id] {
            std::string error;
            client->Scrobble(id, true, &error);
        });
    }
    // The song ran out: on to the next one.
    if (playing_ && stream_->finished.load() && stream_->ring.available() == 0 &&
        error_.empty())
    {
        next();
    }
}

} // namespace navi
