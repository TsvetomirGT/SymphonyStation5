#include "net/http.h"

#include <curl/curl.h>

#include "util/file.h"

#ifdef NAVI_NATIVE
#include "native/console_curl.h"
#endif

namespace {

size_t WriteToString(char* data, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(data, size * nmemb);
  return size * nmemb;
}

}  // namespace

void HttpGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }

void HttpGlobalCleanup() { curl_global_cleanup(); }

HttpResponse HttpGet(const std::string& url, long timeout_seconds) {
  HttpResponse resp;
  CURL* curl = curl_easy_init();
  if (!curl) {
    resp.error = "curl_easy_init failed";
    return resp;
  }

  char errbuf[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // required for threaded use
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "SymphonyStation5/0.1");
#if defined(NAVI_NATIVE)
  // Native title: console CA list, no signals, non-blocking sockets.
  console_curl_setup(curl);
#elif defined(__SCE__)
  // The console has no CA store that OpenSSL knows about, so the payload
  // ships the SDK's bundle next to eboot.elf (only matters for https).
  static const std::string ca_path = AbsPath("ca-bundle.crt");
  curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path.c_str());
#endif

  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    resp.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
  curl_easy_cleanup(curl);
  return resp;
}

std::string UrlEncode(const std::string& s) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0xf]);
    }
  }
  return out;
}
