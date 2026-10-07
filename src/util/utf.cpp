#include "util/utf.h"

namespace {

constexpr char32_t kReplacement = 0xFFFD;

void AppendUtf8(char32_t cp, std::string* out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

std::u16string Utf8ToUtf16(const std::string& in) {
  std::u16string out;
  size_t i = 0;
  while (i < in.size()) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    char32_t cp;
    int extra;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      out.push_back(static_cast<char16_t>(kReplacement));
      i++;
      continue;
    }
    if (i + extra >= in.size()) {  // sequence cut off at the end
      out.push_back(static_cast<char16_t>(kReplacement));
      break;
    }
    bool valid = true;
    for (int k = 1; k <= extra; k++) {
      unsigned char cc = static_cast<unsigned char>(in[i + k]);
      if ((cc & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      out.push_back(static_cast<char16_t>(kReplacement));
      i++;
      continue;
    }
    i += extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

std::string Utf16ToUtf8(const std::u16string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    char32_t cp = in[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size() &&
        in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = kReplacement;  // unpaired surrogate
    }
    AppendUtf8(cp, &out);
  }
  return out;
}

void PopUtf8Char(std::string* s) {
  while (!s->empty()) {
    unsigned char c = static_cast<unsigned char>(s->back());
    s->pop_back();
    if ((c & 0xC0) != 0x80) {
      break;  // removed the lead byte (or an ASCII char)
    }
  }
}
