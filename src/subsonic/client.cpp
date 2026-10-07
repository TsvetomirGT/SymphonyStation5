// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subsonic/client.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <random>

#include "net/http.h"
#include "util/md5.h"

namespace subsonic {

namespace {

constexpr const char* kApiVersion = "1.16.1";
constexpr const char* kClientName = "navistation5";

std::string RandomSalt() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::uniform_int_distribution<int> pick(0, sizeof(kChars) - 2);
  std::string salt(12, ' ');
  for (char& c : salt) {
    c = kChars[pick(rng)];
  }
  return salt;
}

// Every response is {"subsonic-response": {"status": "ok"|"failed", ...}}.
// On success returns the inner object; on failure sets *error.
bool Unwrap(const std::string& body, nlohmann::json* inner,
            std::string* error) {
  auto json = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (json.is_discarded() || !json.contains("subsonic-response")) {
    *error = "unexpected response (not Subsonic JSON)";
    return false;
  }
  *inner = json["subsonic-response"];
  const auto status = inner->is_object() ? inner->find("status") : inner->end();
  if (status == inner->end() || !status->is_string() ||
      status->get<std::string>() != "ok") {
    *error = "API error";
    const auto err = inner->is_object() ? inner->find("error") : inner->end();
    if (err != inner->end() && err->is_object()) {
      const auto message = err->find("message");
      if (message != err->end() && message->is_string()) {
        *error = message->get<std::string>();
      }
    }
    return false;
  }
  return true;
}

// GET a URL, mapping transport and HTTP failures to *error.
bool Fetch(const std::string& url, std::string* body, std::string* error) {
  HttpResponse resp = HttpGet(url);
  if (!resp.error.empty()) {
    *error = resp.error;
    return false;
  }
  if (!resp.ok()) {
    *error = "HTTP " + std::to_string(resp.status);
    return false;
  }
  *body = std::move(resp.body);
  return true;
}

// Type-tolerant field access. With exceptions disabled (native build),
// json::value() on a field of an unexpected type would abort the app, and
// servers differ in what they send (numbers as strings, nulls, ...).
std::string Str(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end()) return "";
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number_integer()) return std::to_string(it->get<long long>());
  return "";
}

int Int(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end()) return 0;
  if (it->is_number()) return static_cast<int>(it->get<double>());
  if (it->is_string()) return std::atoi(it->get<std::string>().c_str());
  return 0;
}

// A list field that may be absent, a single object, or an array.
std::vector<const nlohmann::json*> Items(const nlohmann::json& j,
                                         const char* key) {
  std::vector<const nlohmann::json*> out;
  if (!j.is_object()) return out;
  auto it = j.find(key);
  if (it == j.end()) return out;
  if (it->is_array()) {
    for (const auto& e : *it) {
      if (e.is_object()) out.push_back(&e);
    }
  } else if (it->is_object()) {
    out.push_back(&*it);
  }
  return out;
}

const nlohmann::json& Child(const nlohmann::json& j, const char* key) {
  static const nlohmann::json kNull;
  if (!j.is_object()) return kNull;
  auto it = j.find(key);
  return it == j.end() ? kNull : *it;
}

Album ToAlbum(const nlohmann::json& a) {
  Album album;
  album.id = Str(a, "id");
  album.name = Str(a, "name");
  if (album.name.empty()) album.name = Str(a, "title");
  album.artist = Str(a, "artist");
  album.artist_id = Str(a, "artistId");
  album.cover_art = Str(a, "coverArt");
  album.song_count = Int(a, "songCount");
  album.year = Int(a, "year");
  album.duration = Int(a, "duration");
  return album;
}

Song ToSong(const nlohmann::json& t) {
  Song song;
  song.id = Str(t, "id");
  song.title = Str(t, "title");
  song.artist = Str(t, "artist");
  song.album = Str(t, "album");
  song.album_id = Str(t, "albumId");
  song.cover_art = Str(t, "coverArt");
  song.track = Int(t, "track");
  song.duration = Int(t, "duration");
  return song;
}

const char* AlbumListTypeName(AlbumListType type) {
  switch (type) {
    case AlbumListType::kRecent: return "recent";
    case AlbumListType::kNewest: return "newest";
    case AlbumListType::kAlphabetical: return "alphabeticalByName";
  }
  return "newest";
}

}  // namespace

std::string MakeToken(const std::string& password, const std::string& salt) {
  return Md5Hex(password + salt);
}

Client::Client(Config config) : config_(std::move(config)) {}

std::string Client::BuildUrl(const std::string& endpoint,
                             const std::string& extra_query) const {
  std::string salt = RandomSalt();
  std::string url = config_.server + "/rest/" + endpoint +
                    "?u=" + UrlEncode(config_.username) +
                    "&t=" + MakeToken(config_.password, salt) +
                    "&s=" + salt +
                    "&v=" + kApiVersion +
                    "&c=" + kClientName +
                    "&f=json";
  if (!extra_query.empty()) {
    url += "&" + extra_query;
  }
  return url;
}

std::string Client::CoverArtUrl(const std::string& cover_art, int size) const {
  return BuildUrl("getCoverArt", "id=" + UrlEncode(cover_art) +
                                     "&size=" + std::to_string(size));
}

std::string Client::StreamUrl(const std::string& song_id,
                              int offset_seconds) const {
  std::string extra = "id=" + UrlEncode(song_id) + "&format=mp3&maxBitRate=320";
  if (offset_seconds > 0) {
    extra += "&timeOffset=" + std::to_string(offset_seconds);
  }
  return BuildUrl("stream", extra);
}

bool Client::Ping(std::string* error) const {
  std::string body;
  nlohmann::json inner;
  return Fetch(BuildUrl("ping"), &body, error) && Unwrap(body, &inner, error);
}

bool Client::GetArtists(std::vector<Artist>* out, std::string* error) const {
  std::string body;
  return Fetch(BuildUrl("getArtists"), &body, error) &&
         ParseArtists(body, out, error);
}

bool Client::GetAlbumList(AlbumListType type, int size, int offset,
                          std::vector<Album>* out, std::string* error) const {
  std::string body;
  std::string extra = std::string("type=") + AlbumListTypeName(type) +
                      "&size=" + std::to_string(size) +
                      "&offset=" + std::to_string(offset);
  return Fetch(BuildUrl("getAlbumList2", extra), &body, error) &&
         ParseAlbumList(body, out, error);
}

bool Client::GetArtist(const std::string& id, std::vector<Album>* out,
                       std::string* error) const {
  std::string body;
  return Fetch(BuildUrl("getArtist", "id=" + UrlEncode(id)), &body, error) &&
         ParseArtistAlbums(body, out, error);
}

bool Client::GetAlbum(const std::string& id, Album* album,
                      std::vector<Song>* songs, std::string* error) const {
  std::string body;
  return Fetch(BuildUrl("getAlbum", "id=" + UrlEncode(id)), &body, error) &&
         ParseAlbum(body, album, songs, error);
}

bool Client::Scrobble(const std::string& song_id, bool submission,
                      std::string* error) const {
  std::string body;
  nlohmann::json inner;
  std::string extra = "id=" + UrlEncode(song_id) +
                      "&submission=" + (submission ? "true" : "false");
  return Fetch(BuildUrl("scrobble", extra), &body, error) &&
         Unwrap(body, &inner, error);
}

// getArtists groups artists by index letter:
//   artists.index[] -> { name: "A", artist[]: {id, name, albumCount, coverArt} }
bool ParseArtists(const std::string& body, std::vector<Artist>* out,
                  std::string* error) {
  nlohmann::json inner;
  if (!Unwrap(body, &inner, error)) {
    return false;
  }
  for (const auto* index : Items(Child(inner, "artists"), "index")) {
    for (const auto* a : Items(*index, "artist")) {
      Artist artist;
      artist.id = Str(*a, "id");
      artist.name = Str(*a, "name");
      artist.cover_art = Str(*a, "coverArt");
      artist.album_count = Int(*a, "albumCount");
      out->push_back(std::move(artist));
    }
  }
  return true;
}

// getAlbumList2: albumList2.album[]
bool ParseAlbumList(const std::string& body, std::vector<Album>* out,
                    std::string* error) {
  nlohmann::json inner;
  if (!Unwrap(body, &inner, error)) {
    return false;
  }
  for (const auto* a : Items(Child(inner, "albumList2"), "album")) {
    out->push_back(ToAlbum(*a));
  }
  return true;
}

// getArtist: artist.album[]
bool ParseArtistAlbums(const std::string& body, std::vector<Album>* out,
                       std::string* error) {
  nlohmann::json inner;
  if (!Unwrap(body, &inner, error)) {
    return false;
  }
  for (const auto* a : Items(Child(inner, "artist"), "album")) {
    out->push_back(ToAlbum(*a));
  }
  return true;
}

// getAlbum: album{..., song[]}
bool ParseAlbum(const std::string& body, Album* album, std::vector<Song>* songs,
                std::string* error) {
  nlohmann::json inner;
  if (!Unwrap(body, &inner, error)) {
    return false;
  }
  const nlohmann::json& a = Child(inner, "album");
  *album = ToAlbum(a);
  for (const auto* t : Items(a, "song")) {
    songs->push_back(ToSong(*t));
  }
  return true;
}

}  // namespace subsonic
