// ps5-homebrew-ui - Native system keyboard with bounded UTF-16 buffers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace hui::ps5
{
// The native dialog owns focus while open. Stop passing input to the app until
// poll() returns accepted, cancelled or failed. Open after the triggering button
// is released so that it cannot also activate the keyboard's first key.
class Ime
{
  public:
    enum class State
    {
        idle,
        open,
        accepted,
        cancelled,
        failed
    };
    Ime() = default;
    Ime(const Ime &) = delete;
    Ime &operator=(const Ime &) = delete;
    ~Ime();
    bool open(std::string_view title, std::string_view placeholder, std::string_view value);
    State poll();
    void close();
    const std::string &text() const
    {
        return value_;
    }

  private:
    // Matches the input limit used by ProsperoRadio's console-tested dialog.
    std::array<std::uint16_t, 40> buffer_{};
    std::array<std::uint16_t, 64> title_{};
    std::array<std::uint16_t, 128> placeholder_{};
    std::string value_;
    std::int64_t started_ = 0;
    bool loaded_ = false;
    bool active_ = false;
};
} // namespace hui::ps5
