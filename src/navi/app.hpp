// SymphonyStation5 - the application: login and artist screens, drawn with
// ps5-homebrew-ui in its Fresh theme.
#pragma once

#include "audio/mixer.hpp"
#include "config.h"
#include "core/input.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/renderer.hpp"
#include "navi/covers.hpp"
#include "navi/dashboard.hpp"
#include "navi/library.hpp"
#include "navi/player.hpp"
#include "navi/tasks.hpp"
#include "ui/components/button.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/text_field.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace navi
{

class App
{
  public:
    App(const hui::ui::Fonts &fonts, hui::gfx::Renderer &renderer, hui::audio::Mixer &mixer);

    void update(const hui::InputFrame &input, float dt);
    void compose(hui::gfx::Renderer &renderer);

    // Sounds and rumble asked for this frame (played by main).
    const hui::ui::Feedback &feedback() const
    {
        return feedback_;
    }
    hui::gfx::Color accent() const
    {
        return theme_.primary;
    }

  private:
    enum class Screen
    {
        login,
        dashboard,
    };

    static constexpr int kFieldCount = 3;
    static constexpr int kConnectFocus = kFieldCount; // focus index of the button

    void update_login(const hui::InputFrame &input, float dt);
    void poll_library();
    void connect();
    void open_keyboard();
    void close_keyboard(bool advance);
    void focus_login(int index);

    void draw_login(hui::ui::Canvas &canvas) const;
    void draw_header(hui::ui::Canvas &canvas, const std::string &subtitle,
                     hui::gfx::Color subtitle_color) const;
    void draw_hint(hui::ui::Canvas &canvas, const std::string &hint) const;

    const hui::ui::Fonts &fonts_;
    std::uint32_t glass_texture_;
    hui::ui::Theme theme_;
    TaskPool api_{2, false}; // Navidrome API calls (browsing, scrobbles)
    CoverCache covers_;
    Player player_;
    Dashboard dashboard_;
    hui::ui::Feedback feedback_;
    hui::gfx::DrawList scene_;
    float time_ = 0.0f;

    Screen screen_ = Screen::login;
    Library library_;
    Config config_;          // last config that connected
    bool have_library_ = false;

    // ---- login ----
    std::array<hui::ui::TextField, kFieldCount> fields_;
    hui::ui::PushButton connect_;
    hui::ui::Keyboard keyboard_;
    int login_focus_ = 0;
    bool editing_ = false; // the keyboard is open for fields_[login_focus_]
    bool connecting_ = false;
    std::string login_status_;
    bool login_status_error_ = false;
};

} // namespace navi
