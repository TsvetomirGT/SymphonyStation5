#pragma once

#include <string>
#include <vector>

#include "config.h"

// Minimal (Open)Subsonic API client for Navidrome.
// Docs: https://opensubsonic.netlify.app/docs/api-reference/
//
// All calls are blocking; run them off the UI thread. A Client is safe to
// use from several threads at once (every call makes its own request).

namespace subsonic {

struct Artist {
  std::string id;
  std::string name;
  std::string cover_art;  // id for getCoverArt; may be empty
  int album_count = 0;
};

struct Album {
  std::string id;
  std::string name;
  std::string artist;
  std::string artist_id;
  std::string cover_art;
  int song_count = 0;
  int year = 0;
  int duration = 0;  // seconds
};

struct Song {
  std::string id;
  std::string title;
  std::string artist;
  std::string album;
  std::string album_id;
  std::string cover_art;
  int track = 0;
  int duration = 0;  // seconds
};

// getAlbumList2 orderings used by the app.
enum class AlbumListType {
  kRecent,        // recently played
  kNewest,        // recently added
  kAlphabetical,  // by name
};

class Client {
 public:
  explicit Client(Config config);

  const Config& config() const { return config_; }

  // Builds <server>/rest/<endpoint>?u=..&t=..&s=..&v=..&c=..&f=json[&extra].
  // A fresh random salt is used for every URL, as the API recommends.
  std::string BuildUrl(const std::string& endpoint,
                       const std::string& extra_query = "") const;

  // URL of an album/artist picture, scaled by the server to `size` pixels.
  std::string CoverArtUrl(const std::string& cover_art, int size) const;
  // URL of a song's audio, transcoded to MP3 and starting at
  // `offset_seconds` (Navidrome honours timeOffset when transcoding).
  std::string StreamUrl(const std::string& song_id, int offset_seconds) const;

  // Each returns false and sets *error on transport, HTTP or API failure
  // (e.g. "Wrong username or password").
  bool Ping(std::string* error) const;
  bool GetArtists(std::vector<Artist>* out, std::string* error) const;
  bool GetAlbumList(AlbumListType type, int size, int offset,
                    std::vector<Album>* out, std::string* error) const;
  // Albums of one artist.
  bool GetArtist(const std::string& id, std::vector<Album>* out,
                 std::string* error) const;
  // Songs of one album, in track order.
  bool GetAlbum(const std::string& id, Album* album, std::vector<Song>* songs,
                std::string* error) const;
  // Records a play (submission=true) or "now playing" (false). Plays feed
  // Navidrome's "recently played" list.
  bool Scrobble(const std::string& song_id, bool submission,
                std::string* error) const;

 private:
  Config config_;
};

// Exposed for tests: token = md5(password + salt).
std::string MakeToken(const std::string& password, const std::string& salt);

// Exposed for tests: parse JSON response bodies.
bool ParseArtists(const std::string& body, std::vector<Artist>* out,
                  std::string* error);
bool ParseAlbumList(const std::string& body, std::vector<Album>* out,
                    std::string* error);
bool ParseArtistAlbums(const std::string& body, std::vector<Album>* out,
                       std::string* error);
bool ParseAlbum(const std::string& body, Album* album, std::vector<Song>* songs,
                std::string* error);

}  // namespace subsonic
