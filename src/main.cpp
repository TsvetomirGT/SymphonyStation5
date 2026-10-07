// SymphonyStation5 - entry point of the native PS5 title.
//
// Opens the display (OpenGL through ps5-opengl), the controller and audio,
// then runs the app every frame: read input, update, play the sounds it asked
// for, draw, present. Modelled on ps5-homebrew-ui's src/main.cpp.

#include "audio/cues.hpp"
#include "audio/mixer.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "gfx/renderer.hpp"
#include "navi/app.hpp"
#include "net/http.h"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "ui/fonts.hpp"

#include <GL/glcorearb.h>

#include <cstdint>
#include <span>
#include <string>

namespace
{

constexpr const char *kAssets = "/app0/assets";

bool load_font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    const std::string path = std::string(kAssets) + "/fonts/" + name;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        hui::sys::log("[NAVI] font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

} // namespace

int main()
{
    using namespace hui;
    sys::log("[NAVI] entry");
    HttpGlobalInit();

    ps5::Display display;
    if (!display.open(1920, 1080))
    {
        sys::log("[NAVI] fatal: display open failed");
        sys::park();
    }

    gfx::Renderer renderer;
    gfx::Font regular;
    gfx::Font semibold;
    gfx::Font display_font;
    gfx::Font mono;
    gfx::Font pixel;
    gfx::Font hand;
    ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, "inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, "montserrat-medium.huifont", &display_font, &fonts.display) ||
        !load_font(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, "press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, "patrick-hand.huifont", &hand, &fonts.hand))
    {
        sys::log("[NAVI] fatal: renderer init failed");
        sys::park();
    }

    ps5::Pad pad;
    pad.open();
    InputTracker tracker;
    audio::Mixer mixer;
    ps5::AudioOut audio_out;
    audio_out.start(mixer);
    audio::SoundBank sounds;
    const auto bank = sounds.load(std::string(kAssets) + "/audio/sfx");
    sys::log("[NAVI] sounds files=%d rejected=%d", bank.files, bank.rejected);

    navi::App app(fonts, renderer, mixer);

    PadSample samples[64];
    std::uint64_t frames = 0;
    std::int64_t last_frame_start = sys::monotonic_us();
    for (;;)
    {
        const std::int64_t now = sys::monotonic_us();
        // Animation time is start-to-start; a hitch must not teleport motion.
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last_frame_start) / 1e6f;
        last_frame_start = now;
        if (dt > 0.05f)
            dt = 0.05f;

        const std::size_t count = pad.read(samples);
        const InputFrame input = tracker.update(std::span<const PadSample>(samples, count),
                                                static_cast<std::uint64_t>(now));
        app.update(input, dt);

        const ui::Feedback &feedback = app.feedback();
        for (const audio::CueEvent &event : feedback.cues)
        {
            sounds.play(mixer, event.set == audio::SoundSet::count ? audio::SoundSet::glass : event.set,
                        event);
        }
        if (feedback.rumble_strength > 0.0f)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);
        const gfx::Color accent = app.accent();
        pad.set_light_bar(static_cast<std::uint8_t>(accent.r * 255.0f),
                          static_cast<std::uint8_t>(accent.g * 255.0f),
                          static_cast<std::uint8_t>(accent.b * 255.0f));

        app.compose(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer.present(0, display.width(), display.height());
        if (!display.swap())
        {
            sys::log("[NAVI] fatal: swap failed frame=%llu error=%s",
                     static_cast<unsigned long long>(frames),
                     ps5::egl_error_name(display.last_error()));
            sys::park();
        }
        ++frames;
        if (frames == 1)
        {
            // Keep the splash until there is something to show.
            const bool hidden = sys::hide_splash_screen();
            sys::log("[NAVI] ready splash_hidden=%d", hidden ? 1 : 0);
        }
    }
}
