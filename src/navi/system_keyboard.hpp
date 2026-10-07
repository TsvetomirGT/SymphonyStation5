// SymphonyStation5 - the PS5 system on-screen keyboard (libSceImeDialog),
// loaded at runtime. Native title only; see system_keyboard.cpp.
#pragma once

#include <memory>
#include <string>

namespace navi
{

class SystemKeyboard
{
  public:
    enum class State
    {
        kIdle,
        kRunning,
        kDone,     // Poll() filled *text
        kCanceled, // closed without OK, or failed
    };

    SystemKeyboard();
    ~SystemKeyboard();

    // Opens the overlay; false with *error when it cannot.
    bool Open(const std::string &title, const std::string &initial, bool url, std::string *error);
    // Poll once per frame while open; kDone/kCanceled are reported once.
    State Poll(std::string *text);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace navi
