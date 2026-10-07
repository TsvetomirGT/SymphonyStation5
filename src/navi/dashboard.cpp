#include "navi/dashboard.hpp"

#include "platform/ps5/system.hpp"

#include <cstdio>
#include <utility>

namespace navi
{

using namespace hui;

namespace
{

// Layout on the kit's 1920 x 1080 virtual canvas.
constexpr float kMargin = 80.0f;
constexpr float kContentTop = 150.0f;
constexpr float kContentWidth = 1920.0f - 2.0f * kMargin;
constexpr gfx::Rect kGridBounds{kMargin, kContentTop, kContentWidth, 1080.0f - kContentTop - 40.0f};

const char *kTabNames[] = {"Home", "Albums", "Artists", "Settings", "Now Playing"};

gfx::Color page_text(const ui::Theme &theme)
{
    return theme.page_text.a > 0.0f ? theme.page_text : theme.text;
}
gfx::Color page_muted(const ui::Theme &theme)
{
    return theme.page_text_muted.a > 0.0f ? theme.page_text_muted : theme.text_muted;
}

std::string format_time(int seconds)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
    return buf;
}

std::vector<ui::CardItem> album_cards(const std::vector<subsonic::Album> &albums, bool year)
{
    std::vector<ui::CardItem> items;
    items.reserve(albums.size());
    for (std::size_t i = 0; i < albums.size(); ++i)
    {
        ui::CardItem item;
        item.title = albums[i].name;
        item.subtitle = year && albums[i].year > 0 ? std::to_string(albums[i].year) : albums[i].artist;
        item.tag = static_cast<int>(i);
        items.push_back(std::move(item));
    }
    return items;
}

void style_shelf(ui::Carousel &shelf, const ui::Theme &theme, const char *title)
{
    shelf.style.theme = theme;
    shelf.style.mode = ui::CarouselMode::leading;
    shelf.style.item_width = 236.0f;
    shelf.style.card.art_aspect = 1.0f;
    shelf.style.card.text = ui::CardText::below;
    shelf.style.card.title_size = 22.0f;
    shelf.style.card.subtitle_size = 18.0f;
    shelf.title = title;
}

} // namespace

Dashboard::Dashboard(const ui::Fonts &fonts, const ui::Theme &theme, CoverCache &covers,
                     Player &player, TaskPool &api)
    : fonts_(fonts), theme_(theme), covers_(covers), player_(player), api_(api)
{
    tabs_.style.theme = theme_;
    tabs_.style.kind = ui::TabKind::pill;
    std::vector<ui::TabItem> tabs;
    for (const char *name : kTabNames)
    {
        ui::TabItem item;
        item.label = name;
        tabs.push_back(item);
    }
    tabs_.set_tabs(std::move(tabs));
    tabs_.set_bounds({460.0f, 44.0f, 1040.0f, 56.0f}); // clear of the 16-letter title

    // Home: two shelves of album covers.
    style_shelf(recent_shelf_, theme_, "Recently Played");
    style_shelf(newest_shelf_, theme_, "Recently Added");
    recent_shelf_.set_bounds({kMargin, kContentTop, kContentWidth, 100.0f});
    const float shelf_height = recent_shelf_.preferred_height();
    recent_shelf_.set_bounds({kMargin, kContentTop, kContentWidth, shelf_height});
    newest_shelf_.set_bounds({kMargin, kContentTop + shelf_height + 36.0f, kContentWidth, shelf_height});
    recent_shelf_.art = [this](ui::Canvas &canvas, const gfx::Rect &art, float radius,
                               const ui::CardItem &item, float)
    { draw_cover(canvas, art, radius, recent_[static_cast<std::size_t>(item.tag)].cover_art); };
    newest_shelf_.art = [this](ui::Canvas &canvas, const gfx::Rect &art, float radius,
                               const ui::CardItem &item, float)
    { draw_cover(canvas, art, radius, newest_[static_cast<std::size_t>(item.tag)].cover_art); };

    // Albums and an artist's albums.
    style_albums(albums_grid_, &albums_);
    style_albums(page_grid_, &page_albums_);
    page_grid_.set_bounds({kMargin, kContentTop + 70.0f, kContentWidth, 1080.0f - kContentTop - 110.0f});

    // Artists.
    artists_grid_.style.theme = theme_;
    artists_grid_.style.columns = 6;
    artists_grid_.style.card.art_aspect = 1.0f;
    artists_grid_.style.card.text = ui::CardText::below;
    artists_grid_.style.exits.up = true;
    artists_grid_.set_bounds(kGridBounds);
    artists_grid_.art = [this](ui::Canvas &canvas, const gfx::Rect &art, float radius,
                               const ui::CardItem &item, float)
    { draw_cover(canvas, art, radius, artists_[static_cast<std::size_t>(item.tag)].cover_art); };

    // Settings.
    settings_.style.theme = theme_;
    settings_.style.panel = true;
    settings_.set_bounds({kMargin, kContentTop + 20.0f, 1100.0f, 520.0f});

    // Now Playing: the transport on the left, the queue on the right.
    controls_.style.theme = theme_;
    controls_.style.show_shuffle = false;
    controls_.style.show_repeat = false;
    controls_.style.exits.up = true;
    controls_.style.exits.right = true;
    controls_.set_bounds({kMargin, kContentTop + 60.0f, 1100.0f, 100.0f});
    controls_.set_bounds({kMargin, kContentTop + 60.0f, 1100.0f, controls_.preferred_height()});
    controls_.art = [this](ui::Canvas &canvas, const gfx::Rect &box, float radius)
    {
        const subsonic::Song *song = player_.current();
        draw_cover(canvas, box, radius, song ? song->cover_art : std::string());
    };
    queue_list_.style.theme = theme_;
    queue_list_.style.panel = true;
    queue_list_.set_bounds({1240.0f, kContentTop + 60.0f, 600.0f, 780.0f});

    // An album's tracks.
    page_tracks_.style.theme = theme_;
    page_tracks_.style.panel = true;
    page_tracks_.set_bounds({620.0f, kContentTop, 1220.0f, 1080.0f - kContentTop - 60.0f});

    tabs_.set_active(kHome, true);
}

void Dashboard::style_albums(ui::GridView &grid, const std::vector<subsonic::Album> *albums)
{
    grid.style.theme = theme_;
    grid.style.columns = 6;
    grid.style.card.art_aspect = 1.0f;
    grid.style.card.text = ui::CardText::below;
    grid.style.exits.up = true;
    grid.set_bounds(kGridBounds);
    grid.art = [this, albums](ui::Canvas &canvas, const gfx::Rect &art, float radius,
                              const ui::CardItem &item, float)
    { draw_cover(canvas, art, radius, (*albums)[static_cast<std::size_t>(item.tag)].cover_art); };
}

// ---- loading --------------------------------------------------------------

void Dashboard::start(std::shared_ptr<const subsonic::Client> client,
                      std::vector<subsonic::Artist> artists, std::string login_storage)
{
    client_ = std::move(client);
    login_storage_ = std::move(login_storage);
    ++generation_;
    pages_.clear();
    tab_ = kHome;
    tabs_.set_active(kHome, true);
    on_tabs_ = false;
    home_row_ = 0;
    set_artists(std::move(artists));
    rebuild_settings();
    load_home();
    load_albums();
}

void Dashboard::set_artists(std::vector<subsonic::Artist> artists)
{
    artists_ = std::move(artists);
    std::vector<ui::CardItem> items;
    items.reserve(artists_.size());
    for (std::size_t i = 0; i < artists_.size(); ++i)
    {
        ui::CardItem item;
        item.title = artists_[i].name;
        const int count = artists_[i].album_count;
        item.subtitle = std::to_string(count) + (count == 1 ? " album" : " albums");
        item.tag = static_cast<int>(i);
        items.push_back(std::move(item));
    }
    artists_grid_.set_items(std::move(items));
    artists_grid_.set_focus(0);
}

void Dashboard::load_home()
{
    home_status_ = "Loading ...";
    struct Result
    {
        bool ok = false;
        std::string error;
        std::vector<subsonic::Album> recent;
        std::vector<subsonic::Album> newest;
    };
    auto result = std::make_shared<Result>();
    auto client = client_;
    const std::uint64_t generation = generation_;
    api_.post(
        [client, result]
        {
            result->ok = client->GetAlbumList(subsonic::AlbumListType::kRecent, 24, 0,
                                              &result->recent, &result->error) &&
                         client->GetAlbumList(subsonic::AlbumListType::kNewest, 24, 0,
                                              &result->newest, &result->error);
        },
        [this, result, generation]
        {
            if (generation != generation_)
                return;
            if (!result->ok)
            {
                home_status_ = "Could not load: " + result->error;
                return;
            }
            home_status_.clear();
            recent_ = std::move(result->recent);
            newest_ = std::move(result->newest);
            recent_shelf_.set_items(album_cards(recent_, false));
            newest_shelf_.set_items(album_cards(newest_, false));
            if (recent_.empty() && !newest_.empty())
                home_row_ = 1; // nothing played yet: start on what is new
            recent_shelf_.enter();
            newest_shelf_.enter();
        });
}

void Dashboard::load_albums()
{
    albums_status_ = "Loading ...";
    struct Result
    {
        bool ok = false;
        std::string error;
        std::vector<subsonic::Album> albums;
    };
    auto result = std::make_shared<Result>();
    auto client = client_;
    const std::uint64_t generation = generation_;
    api_.post(
        [client, result]
        {
            // getAlbumList2 returns at most 500 per call: page through.
            constexpr int kPage = 500;
            constexpr int kLimit = 20000;
            for (int offset = 0; offset < kLimit; offset += kPage)
            {
                std::vector<subsonic::Album> page;
                if (!client->GetAlbumList(subsonic::AlbumListType::kAlphabetical, kPage, offset,
                                          &page, &result->error))
                    return;
                const bool last = static_cast<int>(page.size()) < kPage;
                result->albums.insert(result->albums.end(), page.begin(), page.end());
                if (last)
                    break;
            }
            result->ok = true;
        },
        [this, result, generation]
        {
            if (generation != generation_)
                return;
            if (!result->ok)
            {
                albums_status_ = "Could not load albums: " + result->error;
                return;
            }
            albums_status_ = result->albums.empty() ? "No albums on this server." : "";
            albums_ = std::move(result->albums);
            albums_grid_.set_items(album_cards(albums_, false));
            albums_grid_.set_focus(0);
            sys::log("[NAVI] albums loaded: %zu", albums_.size());
        });
}

void Dashboard::open_album(const subsonic::Album &album)
{
    pages_.push_back(Page::album);
    page_album_ = album;
    page_songs_.clear();
    page_tracks_.set_items({});
    page_status_ = "Loading ...";
    struct Result
    {
        bool ok = false;
        std::string error;
        subsonic::Album album;
        std::vector<subsonic::Song> songs;
    };
    auto result = std::make_shared<Result>();
    auto client = client_;
    const std::string id = album.id;
    const std::uint64_t generation = generation_;
    api_.post([client, result, id]
              { result->ok = client->GetAlbum(id, &result->album, &result->songs, &result->error); },
              [this, result, generation, id]
              {
                  if (generation != generation_ || pages_.empty() || pages_.back() != Page::album ||
                      page_album_.id != id)
                      return;
                  if (!result->ok)
                  {
                      page_status_ = "Could not load: " + result->error;
                      return;
                  }
                  page_status_.clear();
                  page_album_ = result->album;
                  page_songs_ = std::move(result->songs);
                  std::vector<ui::ListItem> items;
                  for (const subsonic::Song &song : page_songs_)
                  {
                      ui::ListItem item;
                      item.title = (song.track > 0 ? std::to_string(song.track) + ".  " : "") + song.title;
                      if (song.artist != page_album_.artist)
                          item.subtitle = song.artist;
                      item.value = format_time(song.duration);
                      items.push_back(std::move(item));
                  }
                  page_tracks_.set_items(std::move(items));
                  page_tracks_.set_focus(0);
                  page_tracks_.set_active(true);
                  page_tracks_.enter();
              });
}

void Dashboard::open_artist(const subsonic::Artist &artist)
{
    pages_.push_back(Page::artist);
    page_artist_ = artist;
    page_albums_.clear();
    page_grid_.set_items({});
    page_status_ = "Loading ...";
    struct Result
    {
        bool ok = false;
        std::string error;
        std::vector<subsonic::Album> albums;
    };
    auto result = std::make_shared<Result>();
    auto client = client_;
    const std::string id = artist.id;
    const std::uint64_t generation = generation_;
    api_.post([client, result, id] { result->ok = client->GetArtist(id, &result->albums, &result->error); },
              [this, result, generation, id]
              {
                  if (generation != generation_ || pages_.empty() || pages_.back() != Page::artist ||
                      page_artist_.id != id)
                      return;
                  if (!result->ok)
                  {
                      page_status_ = "Could not load: " + result->error;
                      return;
                  }
                  page_status_ = result->albums.empty() ? "No albums." : "";
                  page_albums_ = std::move(result->albums);
                  page_grid_.set_items(album_cards(page_albums_, true));
                  page_grid_.set_focus(0);
                  page_grid_.enter();
              });
}

void Dashboard::rebuild_settings()
{
    const subsonic::Client *client = client_.get();
    std::vector<ui::ListItem> items;
    ui::ListItem header;
    header.title = "Server";
    header.header = true;
    items.push_back(header);
    ui::ListItem server;
    server.title = "Navidrome server";
    server.value = client ? client->config().server : "";
    items.push_back(server);
    ui::ListItem user;
    user.title = "Username";
    user.value = client ? client->config().username : "";
    items.push_back(user);
    ui::ListItem storage;
    storage.title = "Login saved to";
    storage.subtitle = login_storage_;
    items.push_back(storage);
    ui::ListItem change;
    change.title = "Change server";
    change.chevron = true;
    change.tag = 1;
    items.push_back(change);
    ui::ListItem reload;
    reload.title = "Reload library";
    reload.chevron = true;
    reload.tag = 2;
    items.push_back(reload);
    ui::ListItem about_header;
    about_header.title = "About";
    about_header.header = true;
    items.push_back(about_header);
    ui::ListItem version;
    version.title = "SymphonyStation5";
    version.value = "0.1";
    items.push_back(version);
    settings_.set_items(std::move(items));
    settings_.set_focus(1);
}

// ---- input ----------------------------------------------------------------

void Dashboard::switch_tab(int tab, ui::Feedback &feedback)
{
    if (tab < 0 || tab >= kTabCount || tab == tab_)
        return;
    tab_ = tab;
    pages_.clear();
    tabs_.set_active(tab, false);
    feedback.play(audio::Cue::tab);
    switch (tab_)
    {
    case kHome:
        recent_shelf_.enter();
        newest_shelf_.enter();
        break;
    case kAlbums:
        albums_grid_.enter();
        break;
    case kArtists:
        artists_grid_.enter();
        break;
    case kSettings:
        settings_.enter();
        break;
    default:
        break;
    }
}

Dashboard::Request Dashboard::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    Request request = Request::none;
    sync_now_playing();

    // L1 / R1 turn the tabs from anywhere.
    if (input.is_pressed(Action::page_prev))
        switch_tab(tab_ - 1, feedback);
    else if (input.is_pressed(Action::page_next))
        switch_tab(tab_ + 1, feedback);
    else if (on_tabs_)
    {
        if (tabs_.handle(input, feedback) == ui::Event::changed)
        {
            const int tab = tabs_.active();
            tabs_.set_active(tab_, true); // switch_tab plays and moves it
            switch_tab(tab, feedback);
        }
        else if (input.nav == Direction::down)
        {
            on_tabs_ = false;
            feedback.play(audio::Cue::focus);
        }
    }
    else
    {
        request = update_view(input, feedback);
    }

    tabs_.set_focused(on_tabs_);
    tabs_.update(dt);
    recent_shelf_.set_active(!on_tabs_ && home_row_ == 0);
    newest_shelf_.set_active(!on_tabs_ && home_row_ == 1);
    recent_shelf_.update(dt);
    newest_shelf_.update(dt);
    albums_grid_.set_active(!on_tabs_);
    albums_grid_.update(dt);
    artists_grid_.set_active(!on_tabs_);
    artists_grid_.update(dt);
    settings_.set_active(!on_tabs_);
    settings_.update(dt);
    controls_.update(dt);
    queue_list_.update(dt);
    page_tracks_.update(dt);
    page_grid_.update(dt);
    return request;
}

Dashboard::Request Dashboard::update_view(const InputFrame &input, ui::Feedback &feedback)
{
    if (!pages_.empty())
    {
        update_page(input, feedback);
        return Request::none;
    }
    switch (tab_)
    {
    case kHome:
        update_home(input, feedback);
        break;
    case kAlbums:
    {
        const ui::Event event = albums_grid_.handle(input, feedback);
        if (event == ui::Event::activated && !albums_.empty())
            open_album(albums_[static_cast<std::size_t>(albums_grid_.focus())]);
        else if (albums_grid_.exit() == Direction::up || (albums_.empty() && input.nav == Direction::up))
            on_tabs_ = true;
        break;
    }
    case kArtists:
    {
        const ui::Event event = artists_grid_.handle(input, feedback);
        if (event == ui::Event::activated && !artists_.empty())
            open_artist(artists_[static_cast<std::size_t>(artists_grid_.focus())]);
        else if (artists_grid_.exit() == Direction::up || (artists_.empty() && input.nav == Direction::up))
            on_tabs_ = true;
        break;
    }
    case kSettings:
        return update_settings(input, feedback);
    case kNowPlaying:
        update_now_playing(input, feedback);
        break;
    default:
        break;
    }
    return Request::none;
}

void Dashboard::update_home(const InputFrame &input, ui::Feedback &feedback)
{
    ui::Carousel &shelf = home_row_ == 0 ? recent_shelf_ : newest_shelf_;
    const std::vector<subsonic::Album> &albums = home_row_ == 0 ? recent_ : newest_;
    if (input.nav == Direction::up)
    {
        if (home_row_ == 1 && !recent_.empty())
        {
            home_row_ = 0;
            feedback.play(audio::Cue::focus);
        }
        else
        {
            on_tabs_ = true;
            feedback.play(audio::Cue::focus);
        }
        return;
    }
    if (input.nav == Direction::down)
    {
        if (home_row_ == 0 && !newest_.empty())
        {
            home_row_ = 1;
            feedback.play(audio::Cue::focus);
        }
        return;
    }
    if (shelf.handle(input, feedback) == ui::Event::activated && !albums.empty())
        open_album(albums[static_cast<std::size_t>(shelf.focus())]);
}

void Dashboard::update_page(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back))
    {
        pages_.pop_back();
        feedback.play(audio::Cue::back);
        if (!pages_.empty() && pages_.back() == Page::artist)
            page_grid_.set_active(true);
        return;
    }
    if (pages_.back() == Page::album)
    {
        if (page_songs_.empty())
            return;
        if (page_tracks_.handle(input, feedback) == ui::Event::activated)
        {
            player_.play_queue(page_songs_, page_tracks_.focus());
            switch_tab(kNowPlaying, feedback);
        }
    }
    else
    {
        if (page_grid_.handle(input, feedback) == ui::Event::activated && !page_albums_.empty())
            open_album(page_albums_[static_cast<std::size_t>(page_grid_.focus())]);
    }
}

Dashboard::Request Dashboard::update_settings(const InputFrame &input, ui::Feedback &feedback)
{
    // The list has no exits of its own: up from its first row goes to the tabs.
    if (input.nav == Direction::up && settings_.focus() <= 1)
    {
        on_tabs_ = true;
        feedback.play(audio::Cue::focus);
        return Request::none;
    }
    if (settings_.handle(input, feedback) != ui::Event::activated)
        return Request::none;
    const ui::ListItem &item = settings_.item(settings_.focus());
    if (item.tag == 1)
        return Request::change_server;
    if (item.tag == 2)
    {
        api_.clear_pending();
        auto client = client_;
        auto artists = std::make_shared<std::vector<subsonic::Artist>>();
        const std::uint64_t generation = generation_;
        api_.post(
            [client, artists]
            {
                std::string error;
                client->GetArtists(artists.get(), &error);
            },
            [this, artists, generation]
            {
                if (generation == generation_)
                    set_artists(std::move(*artists));
            });
        load_home();
        load_albums();
    }
    return Request::none;
}

void Dashboard::sync_now_playing()
{
    const subsonic::Song *song = player_.current();
    if (song)
    {
        controls_.title = song->title;
        controls_.artist = song->artist;
        controls_.set_duration(player_.duration());
        controls_.set_position(player_.position());
    }
    else
    {
        controls_.title = "Nothing playing";
        controls_.artist = "Choose an album and press Cross on a track";
        controls_.set_duration(0.0f);
        controls_.set_position(0.0f, true);
    }
    controls_.set_playing(player_.playing());

    // The queue list follows the song.
    const std::string id = song ? song->id : std::string();
    if (id != queue_song_)
    {
        queue_song_ = id;
        std::vector<ui::ListItem> items;
        const auto &queue = player_.queue();
        for (std::size_t i = 0; i < queue.size(); ++i)
        {
            ui::ListItem item;
            item.title = queue[i].title;
            item.subtitle = queue[i].artist;
            item.value = format_time(queue[i].duration);
            if (static_cast<int>(i) == player_.index())
                item.badge = "NOW";
            items.push_back(std::move(item));
        }
        queue_list_.set_items(std::move(items));
        if (player_.index() >= 0)
            queue_list_.set_focus(player_.index());
    }
}

void Dashboard::update_now_playing(const InputFrame &input, ui::Feedback &feedback)
{
    if (on_queue_)
    {
        if (input.nav == Direction::left || input.is_pressed(Action::back))
        {
            on_queue_ = false;
            queue_list_.set_active(false);
            controls_.set_active(true);
            feedback.play(audio::Cue::focus);
        }
        else if (queue_list_.handle(input, feedback) == ui::Event::activated)
        {
            player_.play_queue(player_.queue(), queue_list_.focus());
        }
        return;
    }

    controls_.set_active(true);
    if (controls_.handle(input, feedback) != ui::Event::none)
    {
        switch (controls_.command())
        {
        case ui::MediaCommand::play:
            player_.play();
            break;
        case ui::MediaCommand::pause:
            player_.pause();
            break;
        case ui::MediaCommand::next:
            player_.next();
            break;
        case ui::MediaCommand::previous:
            player_.previous();
            break;
        case ui::MediaCommand::seek:
        case ui::MediaCommand::rewind:
        case ui::MediaCommand::forward:
            player_.seek(controls_.seek_position());
            break;
        case ui::MediaCommand::volume:
        case ui::MediaCommand::mute:
            player_.set_volume(controls_.muted() ? 0.0f : controls_.volume());
            break;
        default:
            break;
        }
    }
    else if (controls_.exit() == Direction::up)
    {
        on_tabs_ = true;
    }
    else if (controls_.exit() == Direction::right && !player_.queue().empty())
    {
        on_queue_ = true;
        controls_.set_active(false);
        queue_list_.set_active(true);
        feedback.play(audio::Cue::focus);
    }
}

// ---- drawing ----------------------------------------------------------------

void Dashboard::draw_cover(ui::Canvas &canvas, const gfx::Rect &rect, float radius,
                           const std::string &cover_id) const
{
    const std::uint32_t texture = covers_.get(cover_id);
    if (texture != 0)
    {
        canvas.list.image(texture, rect, gfx::kFullUv, gfx::Color::rgb(0xffffff), radius);
        return;
    }
    // Placeholder while it loads (or when there is none): a quiet plate.
    canvas.list.rounded_rect(rect, radius, theme_.surface_high);
}

void Dashboard::draw_header(ui::Canvas &canvas) const
{
    ui::text(canvas.list, fonts_.semibold, "SymphonyStation5", kMargin, 84.0f, 34.0f, page_text(theme_));
    tabs_.draw(canvas);
    if (const subsonic::Song *song = player_.current())
    {
        const std::string line = (player_.playing() ? "Playing  " : "Paused  ") + song->title;
        ui::text(canvas.list, fonts_.regular, line, 1920.0f - kMargin, 72.0f, 20.0f,
                 page_text(theme_), gfx::Align::right);
        ui::text(canvas.list, fonts_.regular, song->artist, 1920.0f - kMargin, 98.0f, 18.0f,
                 page_muted(theme_), gfx::Align::right);
    }
}

void Dashboard::draw_message(ui::Canvas &canvas, const std::string &text, float y) const
{
    ui::text(canvas.list, fonts_.regular, text, kMargin, y, 26.0f, page_muted(theme_));
}

void Dashboard::draw_home(ui::Canvas &canvas) const
{
    if (!home_status_.empty())
    {
        draw_message(canvas, home_status_, kContentTop + 60.0f);
        return;
    }
    recent_shelf_.draw(canvas);
    if (recent_.empty())
        draw_message(canvas, "Nothing played yet.", recent_shelf_.bounds().y + 120.0f);
    newest_shelf_.draw(canvas);
}

void Dashboard::draw_page(ui::Canvas &canvas) const
{
    if (pages_.back() == Page::album)
    {
        const gfx::Rect art{kMargin, kContentTop, 480.0f, 480.0f};
        draw_cover(canvas, art, 18.0f, page_album_.cover_art);
        float y = art.y + art.h + 56.0f;
        ui::text(canvas.list, fonts_.semibold, page_album_.name, kMargin, y, 32.0f, page_text(theme_));
        y += 40.0f;
        ui::text(canvas.list, fonts_.regular, page_album_.artist, kMargin, y, 24.0f, page_muted(theme_));
        if (page_album_.year > 0)
        {
            y += 34.0f;
            ui::text(canvas.list, fonts_.regular, std::to_string(page_album_.year), kMargin, y, 22.0f,
                     page_muted(theme_));
        }
        if (!page_status_.empty())
            ui::text(canvas.list, fonts_.regular, page_status_, 640.0f, kContentTop + 60.0f, 26.0f,
                     page_muted(theme_));
        else
            page_tracks_.draw(canvas);
    }
    else
    {
        ui::text(canvas.list, fonts_.semibold, page_artist_.name, kMargin, kContentTop + 40.0f, 36.0f,
                 page_text(theme_));
        if (!page_status_.empty())
            draw_message(canvas, page_status_, kContentTop + 110.0f);
        else
            page_grid_.draw(canvas);
    }
}

void Dashboard::draw_now_playing(ui::Canvas &canvas) const
{
    controls_.draw(canvas);
    if (!player_.error().empty())
    {
        const gfx::Rect bounds = controls_.bounds();
        ui::text(canvas.list, fonts_.regular, "Playback error: " + player_.error(), kMargin,
                 bounds.y + bounds.h + 50.0f, 24.0f, theme_.danger);
    }
    else if (player_.buffering())
    {
        const gfx::Rect bounds = controls_.bounds();
        ui::text(canvas.list, fonts_.regular, "Buffering ...", kMargin, bounds.y + bounds.h + 50.0f,
                 24.0f, page_muted(theme_));
    }
    if (!player_.queue().empty())
    {
        ui::text(canvas.list, fonts_.semibold, "Up next", queue_list_.bounds().x, kContentTop + 40.0f,
                 24.0f, page_text(theme_));
        queue_list_.draw(canvas);
    }
}

void Dashboard::draw(ui::Canvas &canvas) const
{
    draw_header(canvas);
    if (!pages_.empty())
    {
        draw_page(canvas);
        return;
    }
    switch (tab_)
    {
    case kHome:
        draw_home(canvas);
        break;
    case kAlbums:
        if (!albums_status_.empty())
            draw_message(canvas, albums_status_, kContentTop + 60.0f);
        else
            albums_grid_.draw(canvas);
        break;
    case kArtists:
        if (artists_.empty())
            draw_message(canvas, "No artists on this server.", kContentTop + 60.0f);
        else
            artists_grid_.draw(canvas);
        break;
    case kSettings:
        settings_.draw(canvas);
        break;
    case kNowPlaying:
        draw_now_playing(canvas);
        break;
    default:
        break;
    }
}

} // namespace navi
