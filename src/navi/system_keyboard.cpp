// System on-screen keyboard (IME dialog). Parked: the login screen uses the
// kit's controller keyboard; this is the planned upgrade. On hardware,
// Begin() reported "Keyboard unavailable" (cause not yet known).
//
// A native title does not get libSceImeDialog loaded, and calling its
// imports would jump to address 0 (runtime.cpp stubs them for SDL). So the
// module is loaded at runtime with sceKernelLoadStartModule and its entry
// points are looked up by name with sceKernelDlsym.
//
// The parameter/result layouts are the ones the PS5 SDL port
// (ps5-payload-dev/SDL, src/video/ps5/SDL_ps5keyboard.c) uses on hardware.
// wchar_t is 16-bit on the PS5 target, so text buffers are char16_t.
//
// Compiled only into the native title (NAVI_NATIVE).
#ifdef NAVI_NATIVE

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "navi/system_keyboard.hpp"
#include "util/utf.h"

extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv,
                             uint32_t flags, void* opt, int* result);
int sceKernelDlsym(int handle, const char* symbol, void** address);
const char* sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetForegroundUser(int* user_id);
}

namespace {

enum ImeType : int32_t {
  kImeTypeDefault = 0,
  kImeTypeBasicLatin = 1,
  kImeTypeUrl = 2,
};

enum ImeDialogStatus : int32_t {
  kImeDialogStatusNone = 0,
  kImeDialogStatusRunning = 1,
  kImeDialogStatusFinished = 2,
};

enum ImeEndStatus : int32_t {
  kImeEndStatusOk = 0,
  kImeEndStatusUserCanceled = 1,
  kImeEndStatusAborted = 2,
};

struct ImeDialogParam {
  int32_t user_id;
  int32_t type;
  uint64_t supported_languages;
  int32_t enter_label;
  int32_t input_method;
  void* filter;
  uint32_t option;
  uint32_t max_text_length;
  char16_t* input_text_buffer;
  float posx;
  float posy;
  int32_t halign;
  int32_t valign;
  const char16_t* placeholder;
  const char16_t* title;
  int8_t reserved[16];
};

struct ImeDialogResult {
  int32_t end_status;
  int8_t reserved[12];
};

using ImeDialogInitFn = int (*)(const ImeDialogParam*, void*);
using ImeDialogGetStatusFn = int32_t (*)(void);
using ImeDialogGetResultFn = int (*)(ImeDialogResult*);
using ImeDialogTermFn = int (*)(void);

constexpr uint32_t kMaxTextLength = 255;

}  // namespace

namespace navi {

class SystemKeyboard::Impl {
 public:
  bool Begin(const std::string& title, const std::string& initial,
             bool url, std::string* error) {
    if (!Load(error)) {
      return false;
    }
    int user_id = -1;
    int rc = sceUserServiceGetForegroundUser(&user_id);
    if (rc != 0) {
      *error = Hex("sceUserServiceGetForegroundUser", rc);
      return false;
    }

    title_ = Utf8ToUtf16(title);
    std::u16string text = Utf8ToUtf16(initial);
    if (text.size() > kMaxTextLength) {
      text.resize(kMaxTextLength);
    }
    std::memset(buffer_, 0, sizeof(buffer_));
    std::memcpy(buffer_, text.data(), text.size() * sizeof(char16_t));

    ImeDialogParam param;
    std::memset(&param, 0, sizeof(param));
    param.user_id = user_id;
    param.type = url ? kImeTypeUrl : kImeTypeDefault;
    param.max_text_length = kMaxTextLength;
    param.input_text_buffer = buffer_;
    param.title = title_.c_str();

    rc = init_(&param, nullptr);
    if (rc != 0) {
      *error = Hex("sceImeDialogInit", rc);
      return false;
    }
    editing_ = true;
    return true;
  }

  SystemKeyboard::State Poll(std::string* text) {
    if (!editing_) {
      return SystemKeyboard::State::kIdle;
    }
    int32_t status = get_status_();
    if (status == kImeDialogStatusRunning) {
      return SystemKeyboard::State::kRunning;
    }
    SystemKeyboard::State state = SystemKeyboard::State::kCanceled;
    if (status == kImeDialogStatusFinished) {
      ImeDialogResult result;
      std::memset(&result, 0, sizeof(result));
      if (get_result_(&result) == 0 && result.end_status == kImeEndStatusOk) {
        *text = Utf16ToUtf8(buffer_);
        state = SystemKeyboard::State::kDone;
      }
    }
    term_();
    editing_ = false;
    return state;
  }

 private:
  static std::string Hex(const char* what, int rc) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s failed: 0x%08x", what,
                  static_cast<unsigned>(rc));
    return buf;
  }

  bool Load(std::string* error) {
    if (init_) {
      return true;
    }
    // Inside the sandbox, /system/common/lib is reachable under a random
    // per-boot directory name (e.g. /m9SAYAYILZ/common/lib).
    const char* word = sceKernelGetFsSandboxRandomWord();
    std::string path = std::string("/") + (word ? word : "system") +
                       "/common/lib/libSceImeDialog.sprx";
    int handle = sceKernelLoadStartModule(path.c_str(), 0, nullptr, 0,
                                          nullptr, nullptr);
    if (handle < 0) {
      *error = Hex(("load " + path).c_str(), handle);
      return false;
    }
    sceUserServiceInitialize(nullptr);  // may already be initialised

    void* init = nullptr;
    void* get_status = nullptr;
    void* get_result = nullptr;
    void* term = nullptr;
    if (sceKernelDlsym(handle, "sceImeDialogInit", &init) != 0 ||
        sceKernelDlsym(handle, "sceImeDialogGetStatus", &get_status) != 0 ||
        sceKernelDlsym(handle, "sceImeDialogGetResult", &get_result) != 0 ||
        sceKernelDlsym(handle, "sceImeDialogTerm", &term) != 0) {
      *error = "libSceImeDialog: missing symbols";
      return false;
    }
    init_ = reinterpret_cast<ImeDialogInitFn>(init);
    get_status_ = reinterpret_cast<ImeDialogGetStatusFn>(get_status);
    get_result_ = reinterpret_cast<ImeDialogGetResultFn>(get_result);
    term_ = reinterpret_cast<ImeDialogTermFn>(term);
    return true;
  }

  bool editing_ = false;
  ImeDialogInitFn init_ = nullptr;
  ImeDialogGetStatusFn get_status_ = nullptr;
  ImeDialogGetResultFn get_result_ = nullptr;
  ImeDialogTermFn term_ = nullptr;

  std::u16string title_;
  char16_t buffer_[kMaxTextLength + 1];
};

SystemKeyboard::SystemKeyboard() : impl_(std::make_unique<Impl>()) {}
SystemKeyboard::~SystemKeyboard() = default;

bool SystemKeyboard::Open(const std::string& title, const std::string& initial,
                          bool url, std::string* error) {
  return impl_->Begin(title, initial, url, error);
}

SystemKeyboard::State SystemKeyboard::Poll(std::string* text) {
  return impl_->Poll(text);
}

}  // namespace navi

#endif  // NAVI_NATIVE
