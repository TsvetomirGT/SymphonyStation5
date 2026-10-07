// SymphonyStation5
// Copyright (C) 2026 tsvetomirgt
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

// UTF-8 <-> UTF-16 conversion. The PS5 system keyboard (IME dialog) works
// in UTF-16 (wchar_t is 16-bit on the PS5 target); the app uses UTF-8.
// Invalid input is replaced with U+FFFD rather than rejected.
std::u16string Utf8ToUtf16(const std::string& in);
std::string Utf16ToUtf8(const std::u16string& in);

// Remove the last UTF-8 code point (for backspace in text fields).
void PopUtf8Char(std::string* s);
