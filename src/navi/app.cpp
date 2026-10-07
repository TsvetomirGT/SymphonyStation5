// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#include "navi/app.hpp"

#include "platform/ps5/system.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace navi
{

using namespace hui;

namespace
{

// Layout on the kit's 1920 x 1080 virtual canvas.
constexpr float kLeft = 160.0f;
constexpr float kFormWidth = 700.0f;
constexpr float kFormTop = 270.0f;
constexpr float kFieldStep = 150.0f;
constexpr gfx::Rect kKeyboardBounds{960.0f, 300.0f, 800.0f, 400.0f};

ui::Theme find_theme(const char *id)
{
    for (const ui::Theme &theme : ui::themes())
    {
        if (std::strcmp(theme.id, id) == 0)
            return theme;
    }
    return ui::default_theme();
}

// Text drawn straight on the page: the theme's page colours when it has them.
gfx::Color page_text(const ui::Theme &theme)
{
    return theme.page_text.a > 0.0f ? theme.page_text : theme.text;
}
gfx::Color page_muted(const ui::Theme &theme)
{
    return theme.page_text_muted.a > 0.0f ? theme.page_text_muted : theme.text_muted;
}

} // namespace

App::App(const ui::Fonts &fonts, gfx::Renderer &renderer, audio::Mixer &mixer)
    : fonts_(fonts), glass_texture_(renderer.glass_texture()), theme_(find_theme("fresh")),
      covers_(renderer), player_(mixer, api_), dashboard_(fonts, theme_, covers_, player_, api_)
{
    const char *labels[kFieldCount] = {"Server URL", "Username", "Password"};
    const char *placeholders[kFieldCount] = {"http://192.168.0.10:4533", "", ""};
    for (int i = 0; i < kFieldCount; ++i)
    {
        ui::TextField &field = fields_[i];
        field.style.theme = theme_;
        field.style.on_page = true;
        field.style.counter = false;
        field.set_label(labels[i]);
        field.set_placeholder(placeholders[i]);
        field.set_bounds({kLeft, kFormTop + i * kFieldStep, kFormWidth, field.preferred_height()});
    }
    fields_[2].style.password = true;

    connect_.style.theme = theme_;
    connect_.label = "Connect";
    connect_.set_bounds({kLeft, kFormTop + kFieldCount * kFieldStep + 10.0f, kFormWidth, 64.0f});

    keyboard_.style.theme = theme_;
    keyboard_.style.bindings = ui::KeyboardBindings::standard();
    keyboard_.set_bounds(kKeyboardBounds);
    keyboard_.on_text = [this](std::string_view utf8) { fields_[login_focus_].insert(utf8); };
    keyboard_.on_backspace = [this] { fields_[login_focus_].backspace(); };

    // Saved credentials fill the form and connect straight away.
    Config saved;
    std::string source;
    std::string error;
    const bool have_saved = LoadSavedConfig(&saved, &source, &error);
    sys::log("[NAVI] saved login: %s", have_saved ? source.c_str() : error.c_str());
    if (have_saved)
    {
        fields_[0].set_text(saved.server);
        fields_[1].set_text(saved.username);
        fields_[2].set_text(saved.password);
        focus_login(kConnectFocus);
        connect();
    }
    else
    {
        focus_login(0);
        // Says why no saved login was found, so a storage problem shows.
        login_status_ = "Enter your Navidrome server details.  (No saved login: " + error + ")";
    }
}

// ---- update ---------------------------------------------------------------

void App::update(const InputFrame &input, float dt)
{
    feedback_.clear();
    time_ += dt;
    api_.pump();
    covers_.pump();
    player_.update(dt);
    poll_library();
    if (screen_ == Screen::login)
    {
        update_login(input, dt);
    }
    else if (dashboard_.update(input, dt, feedback_) == Dashboard::Request::change_server)
    {
        screen_ = Screen::login;
        focus_login(0);
        login_status_ = "Edit the server details, then Connect.";
        login_status_error_ = false;
        feedback_.play(audio::Cue::modal_open);
    }
}

void App::poll_library()
{
    Library::Result result;
    if (!library_.take(&result))
        return;
    connecting_ = false;
    connect_.set_loading(false);
    if (!result.ok)
    {
        sys::log("[NAVI] connect %s failed: %s", result.config.server.c_str(),
                 result.error.c_str());
        login_status_ = "Could not connect: " + result.error;
        login_status_error_ = true;
        feedback_.play(audio::Cue::error);
        return;
    }

    sys::log("[NAVI] connected to %s: %zu artists", result.config.server.c_str(),
             result.artists.size());
    config_ = result.config;
    have_library_ = true;
    std::vector<std::string> saved;
    std::string error;
    SaveConfigEverywhere(config_, &saved, &error);
    std::string storage;
    for (const std::string &path : saved)
        storage += (storage.empty() ? "" : ", ") + path;
    if (storage.empty())
        storage = "not saved";
    if (!error.empty())
        storage += "  (failed: " + error + ")";
    sys::log("[NAVI] login saved: %s", storage.c_str());

    auto client = std::make_shared<const subsonic::Client>(config_);
    covers_.set_client(client);
    player_.set_client(client);
    dashboard_.start(client, std::move(result.artists), storage);
    screen_ = Screen::dashboard;
    feedback_.play(audio::Cue::complete);
}

void App::focus_login(int index)
{
    login_focus_ = index;
    for (int i = 0; i < kFieldCount; ++i)
        fields_[i].set_active(i == index);
    connect_.set_active(index == kConnectFocus);
}

void App::open_keyboard()
{
    editing_ = true;
    keyboard_.set_layout(0);
    keyboard_.set_active(true);
    keyboard_.enter();
    feedback_.play(audio::Cue::modal_open);
}

void App::close_keyboard(bool advance)
{
    editing_ = false;
    keyboard_.set_active(false);
    feedback_.play(audio::Cue::modal_close);
    if (advance)
        focus_login(login_focus_ + 1); // the next field, or the Connect button
}

void App::connect()
{
    Config config{NormalizeServerUrl(fields_[0].text()), fields_[1].text(), fields_[2].text()};
    if (config.server.empty() || config.username.empty())
    {
        login_status_ = "Server URL and username are required.";
        login_status_error_ = true;
        feedback_.play(audio::Cue::error);
        return;
    }
    fields_[0].set_text(config.server);
    if (!library_.start(config))
        return;
    connecting_ = true;
    connect_.set_loading(true);
    login_status_ = "Connecting to " + config.server + " ...";
    login_status_error_ = false;
}

void App::update_login(const InputFrame &input, float dt)
{
    if (editing_)
    {
        ui::TextField &field = fields_[login_focus_];
        if (input.is_pressed(Action::back))
        {
            close_keyboard(false);
        }
        else
        {
            keyboard_.set_length(field.length());
            const ui::Event event = keyboard_.handle(input, feedback_);
            if (event == ui::Event::activated) // the Done key
                close_keyboard(true);
            else if (event == ui::Event::cancelled)
                close_keyboard(false);
        }
    }
    else if (!connecting_)
    {
        if (input.nav == Direction::up && login_focus_ > 0)
        {
            focus_login(login_focus_ - 1);
            feedback_.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::down && login_focus_ < kConnectFocus)
        {
            focus_login(login_focus_ + 1);
            feedback_.play(audio::Cue::focus);
        }
        else if (input.is_pressed(Action::confirm))
        {
            if (login_focus_ < kFieldCount)
            {
                feedback_.play(audio::Cue::select);
                open_keyboard();
            }
            else
            {
                connect_.press();
                feedback_.play(audio::Cue::select);
                connect();
            }
        }
        else if (input.is_pressed(Action::back) && have_library_)
        {
            screen_ = Screen::dashboard;
            feedback_.play(audio::Cue::back);
        }
    }

    for (ui::TextField &field : fields_)
        field.update(dt);
    connect_.update(dt);
    keyboard_.update(dt);
}

// ---- drawing --------------------------------------------------------------

void App::compose(gfx::Renderer &renderer)
{
    scene_.clear();
    ui::Canvas canvas{scene_, fonts_, glass_texture_, time_};
    if (screen_ == Screen::login)
        draw_login(canvas);
    else
        dashboard_.draw(canvas);

    gfx::BackdropSpec backdrop = theme_.backdrop;
    backdrop.time = time_;
    renderer.begin();
    renderer.backdrop(backdrop);
    renderer.draw(scene_);
}

void App::draw_header(ui::Canvas &canvas, const std::string &subtitle,
                      gfx::Color subtitle_color) const
{
    ui::text(canvas.list, fonts_.semibold, "SymphonyStation5", kLeft, 150.0f, 64.0f,
             page_text(theme_));
    ui::text(canvas.list, fonts_.regular, subtitle, kLeft, 205.0f, 28.0f, subtitle_color);
}

void App::draw_hint(ui::Canvas &canvas, const std::string &hint) const
{
    ui::text(canvas.list, fonts_.regular, hint, kLeft, 1040.0f, 24.0f, page_muted(theme_));
}

void App::draw_login(ui::Canvas &canvas) const
{
    draw_header(canvas, "Connect to your Navidrome server", page_muted(theme_));
    for (const ui::TextField &field : fields_)
        field.draw(canvas);
    connect_.draw(canvas);

    const gfx::Rect button = connect_.bounds();
    ui::text(canvas.list, fonts_.regular, login_status_, kLeft, button.y + button.h + 50.0f,
             24.0f, login_status_error_ ? theme_.danger : page_muted(theme_));

    if (editing_)
    {
        keyboard_.draw(canvas);
        draw_hint(canvas, "Square: delete    Triangle: space    L2: shift    R2: symbols    "
                          "Circle: close keyboard");
    }
    else
    {
        draw_hint(canvas, have_library_ ? "Cross: edit / select    D-pad: move    Circle: back"
                                        : "Cross: edit / select    D-pad: move");
    }
}

} // namespace navi
