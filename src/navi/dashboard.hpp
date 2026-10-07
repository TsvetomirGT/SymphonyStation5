// SymphonyStation5 - the main screen once logged in: a tab bar on top (Home,
// Albums, Artists, Settings, Now Playing) over the selected view.
#pragma once

#include "core/input.hpp"
#include "navi/covers.hpp"
#include "navi/player.hpp"
#include "navi/tasks.hpp"
#include "subsonic/client.h"
#include "ui/components/carousel.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/list.hpp"
#include "ui/components/media_controls.hpp"
#include "ui/components/tabs.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace navi
{

class Dashboard
{
  public:
    enum class Request
    {
        none,
        change_server, // Settings asked to edit the server details
    };

    Dashboard(const hui::ui::Fonts &fonts, const hui::ui::Theme &theme, CoverCache &covers,
              Player &player, TaskPool &api);

    // Starts over with a server that just connected; `artists` came with the
    // login check, `login_storage` says where the login was saved (shown in
    // Settings).
    void start(std::shared_ptr<const subsonic::Client> client,
               std::vector<subsonic::Artist> artists, std::string login_storage);

    Request update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void draw(hui::ui::Canvas &canvas) const;

  private:
    enum Tab
    {
        kHome,
        kAlbums,
        kArtists,
        kSettings,
        kNowPlaying,
        kTabCount,
    };
    // A page opened from a view, drawn instead of it until Back.
    enum class Page
    {
        album,  // an album's tracks
        artist, // an artist's albums
    };

    // ---- loading ----
    void load_home();
    void load_albums();
    void open_album(const subsonic::Album &album);
    void open_artist(const subsonic::Artist &artist);
    void set_artists(std::vector<subsonic::Artist> artists);
    void rebuild_settings();

    // ---- input ----
    void switch_tab(int tab, hui::ui::Feedback &feedback);
    Request update_view(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void update_home(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void update_page(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    Request update_settings(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void update_now_playing(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void sync_now_playing();

    // ---- drawing ----
    void draw_header(hui::ui::Canvas &canvas) const;
    void draw_home(hui::ui::Canvas &canvas) const;
    void draw_page(hui::ui::Canvas &canvas) const;
    void draw_now_playing(hui::ui::Canvas &canvas) const;
    void draw_message(hui::ui::Canvas &canvas, const std::string &text, float y) const;
    void draw_cover(hui::ui::Canvas &canvas, const hui::gfx::Rect &rect, float radius,
                    const std::string &cover_id) const;
    void style_albums(hui::ui::GridView &grid, const std::vector<subsonic::Album> *albums);

    const hui::ui::Fonts &fonts_;
    const hui::ui::Theme &theme_;
    CoverCache &covers_;
    Player &player_;
    TaskPool &api_;
    std::shared_ptr<const subsonic::Client> client_;
    std::uint64_t generation_ = 0; // bumps on start(); stale results are dropped

    hui::ui::TabBar tabs_;
    int tab_ = kHome;
    bool on_tabs_ = false; // the focus is on the tab bar, not the view

    // Home: two shelves.
    hui::ui::Carousel recent_shelf_;
    hui::ui::Carousel newest_shelf_;
    std::vector<subsonic::Album> recent_;
    std::vector<subsonic::Album> newest_;
    int home_row_ = 0;
    std::string home_status_;

    // Albums and Artists grids.
    hui::ui::GridView albums_grid_;
    std::vector<subsonic::Album> albums_;
    std::string albums_status_;
    hui::ui::GridView artists_grid_;
    std::vector<subsonic::Artist> artists_;

    // Settings.
    hui::ui::ListView settings_;
    std::string login_storage_;

    // Now Playing.
    hui::ui::MediaControls controls_;
    hui::ui::ListView queue_list_;
    std::string queue_song_; // song id the queue list was built for
    bool on_queue_ = false;  // the focus is on the queue, not the transport

    // Pages opened over the current tab (artist, then one of its albums);
    // Back closes the top one.
    std::vector<Page> pages_;
    subsonic::Album page_album_;
    std::vector<subsonic::Song> page_songs_;
    hui::ui::ListView page_tracks_;
    subsonic::Artist page_artist_;
    std::vector<subsonic::Album> page_albums_;
    hui::ui::GridView page_grid_;
    std::string page_status_;
};

} // namespace navi
