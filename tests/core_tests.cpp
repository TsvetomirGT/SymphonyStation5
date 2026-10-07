// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Desktop-only unit checks for the platform-independent core.
// Run: make test

#include <cstdio>
#include <string>
#include <vector>

#include <unistd.h>

#include "config.h"
#include "subsonic/client.h"
#include "util/md5.h"
#include "util/utf.h"

static int g_failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                              \
    }                                                            \
  } while (0)

static void TestMd5() {
  // RFC 1321 test vectors.
  CHECK(Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
  CHECK(Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
  CHECK(Md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
  CHECK(Md5Hex("12345678901234567890123456789012345678901234567890123456789"
               "012345678901234567890") ==
        "57edf4a22be3c955ac49da2e2107b67a");
}

static void TestToken() {
  // Example from the Subsonic API docs: password "sesame", salt "c19b2d".
  CHECK(subsonic::MakeToken("sesame", "c19b2d") ==
        "26719a1196d2a940705a59634eb18eab");
}

static void TestBuildUrl() {
  Config cfg{"http://nas:4533", "me & you", "pw"};
  subsonic::Client client(cfg);
  std::string url = client.BuildUrl("getAlbum", "id=42");
  CHECK(url.rfind("http://nas:4533/rest/getAlbum?u=me%20%26%20you&t=", 0) == 0);
  CHECK(url.find("&v=1.16.1&c=navistation5&f=json&id=42") != std::string::npos);
  CHECK(url.find("pw") == std::string::npos);  // password never sent in clear
}

static void TestParseArtists() {
  const char* body = R"({"subsonic-response":{"status":"ok","version":"1.16.1",
    "artists":{"index":[
      {"name":"A","artist":[{"id":"a1","name":"ABBA","albumCount":9}]},
      {"name":"B","artist":[{"id":"b1","name":"Björk","albumCount":3},
                            {"id":"b2","name":"Blur"}]}]}}})";
  std::vector<subsonic::Artist> artists;
  std::string error;
  CHECK(subsonic::ParseArtists(body, &artists, &error));
  CHECK(artists.size() == 3);
  CHECK(artists[1].name == "Björk");
  CHECK(artists[0].album_count == 9);
  CHECK(artists[2].album_count == 0);

  const char* failed = R"({"subsonic-response":{"status":"failed",
    "error":{"code":40,"message":"Wrong username or password"}}})";
  artists.clear();
  CHECK(!subsonic::ParseArtists(failed, &artists, &error));
  CHECK(error == "Wrong username or password");

  CHECK(!subsonic::ParseArtists("<html>", &artists, &error));
}

static void TestUtf() {
  std::string s = "Bj\xC3\xB6rk \xE2\x82\xAC \xF0\x9F\x8E\xB5";  // Björk € 🎵
  std::u16string u = Utf8ToUtf16(s);
  CHECK(u.size() == 10);  // 🎵 is a surrogate pair
  CHECK(u[2] == 0x00F6);
  CHECK(Utf16ToUtf8(u) == s);
  CHECK(Utf16ToUtf8(Utf8ToUtf16("abc")) == "abc");
  CHECK(Utf8ToUtf16("a\xC3") == u"a\uFFFD");  // truncated sequence

  std::string t = "ab\xC3\xB6";
  PopUtf8Char(&t);
  CHECK(t == "ab");
  PopUtf8Char(&t);
  CHECK(t == "a");
}

static void TestConfig() {
  CHECK(NormalizeServerUrl("  192.168.0.10:4533/ ") ==
        "http://192.168.0.10:4533");
  CHECK(NormalizeServerUrl("https://music.example.org//") ==
        "https://music.example.org");
  CHECK(NormalizeServerUrl("   ") == "");

  char dir[] = "/tmp/navi-test-XXXXXX";
  CHECK(mkdtemp(dir) != nullptr);
  std::string path = std::string(dir) + "/config.json";
  Config in{"http://nas:4533", "me", "p\"w"};
  std::string error;
  CHECK(SaveConfig(path, in, &error));
  Config out;
  CHECK(LoadConfig(path, &out, &error));
  CHECK(out.server == in.server && out.username == in.username &&
        out.password == in.password);
  unlink(path.c_str());
  rmdir(dir);
}

static void TestAlbums() {
  const char* list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"al1","name":"Homogenic","artist":"Björk","artistId":"ar1",
     "coverArt":"al-al1","songCount":10,"year":1997,"duration":2627},
    {"id":"al2","name":"Debut","artist":"Björk","year":"1993"}]}}})";
  std::vector<subsonic::Album> albums;
  std::string error;
  CHECK(subsonic::ParseAlbumList(list, &albums, &error));
  CHECK(albums.size() == 2);
  CHECK(albums[0].cover_art == "al-al1" && albums[0].song_count == 10);
  CHECK(albums[1].year == 1993);  // number sent as a string

  // A single object instead of an array, nulls and odd types must not crash.
  const char* odd = R"({"subsonic-response":{"status":"ok","albumList2":{"album":
    {"id":7,"name":null,"songCount":"x"}}}})";
  albums.clear();
  CHECK(subsonic::ParseAlbumList(odd, &albums, &error));
  CHECK(albums.size() == 1 && albums[0].id == "7" && albums[0].name.empty());

  const char* empty = R"({"subsonic-response":{"status":"ok","albumList2":{}}})";
  albums.clear();
  CHECK(subsonic::ParseAlbumList(empty, &albums, &error) && albums.empty());

  const char* album = R"({"subsonic-response":{"status":"ok","album":{
    "id":"al1","name":"Homogenic","artist":"Björk","coverArt":"al-al1",
    "song":[{"id":"s1","title":"Hunter","track":1,"duration":255,"artist":"Björk"},
            {"id":"s2","title":"Jóga","track":2,"duration":305}]}}})";
  subsonic::Album a;
  std::vector<subsonic::Song> songs;
  CHECK(subsonic::ParseAlbum(album, &a, &songs, &error));
  CHECK(a.name == "Homogenic" && songs.size() == 2);
  CHECK(songs[1].title == "Jóga" && songs[1].duration == 305);

  const char* artist = R"({"subsonic-response":{"status":"ok","artist":{
    "id":"ar1","name":"Björk","album":[{"id":"al1","name":"Homogenic"}]}}})";
  albums.clear();
  CHECK(subsonic::ParseArtistAlbums(artist, &albums, &error));
  CHECK(albums.size() == 1 && albums[0].name == "Homogenic");

  const char* weird_error = R"({"subsonic-response":{"status":"failed","error":{"message":42}}})";
  CHECK(!subsonic::ParseAlbumList(weird_error, &albums, &error));
  CHECK(error == "API error");
}

static void TestMediaUrls() {
  subsonic::Client client(Config{"http://nas:4533", "me", "pw"});
  std::string cover = client.CoverArtUrl("al-1 2", 300);
  CHECK(cover.find("/rest/getCoverArt?") != std::string::npos);
  CHECK(cover.find("&id=al-1%202&size=300") != std::string::npos);
  std::string stream = client.StreamUrl("s1", 0);
  CHECK(stream.find("&id=s1&format=mp3&maxBitRate=320") != std::string::npos);
  CHECK(stream.find("timeOffset") == std::string::npos);
  CHECK(client.StreamUrl("s1", 42).find("&timeOffset=42") != std::string::npos);
}

int main() {
  TestAlbums();
  TestMediaUrls();
  TestUtf();
  TestConfig();
  TestMd5();
  TestToken();
  TestBuildUrl();
  TestParseArtists();
  if (g_failures == 0) {
    std::printf("all tests passed\n");
  }
  return g_failures == 0 ? 0 : 1;
}
