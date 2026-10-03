// ProsperoStore - Bounded HTTPS transport shared by host and console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>

namespace store::net
{
// Console startup/shutdown, outside the worker lifetime.
int start_transport(bool elevated);
void stop_transport();
enum class Purpose
{
    catalog,
    artifact
};
struct Control
{
    std::atomic<bool> cancelled{false};
    std::mutex guard;
    int request = -1;
    // Owned by one worker; release after joining it, before transport shutdown.
    std::unique_ptr<void, void (*)(void *)> connection{nullptr, nullptr};
    void cancel();
};
struct Response
{
    int status = 0;
    std::uint64_t bytes = 0;
    std::string location, etag, error;
    bool ok() const
    {
        return error.empty() && (status == 200 || status == 304);
    }
};
using Sink = std::function<bool(std::string_view)>;
bool read_headers(std::string_view headers, Response &out);
Response request_once(const std::string &url, std::uint64_t limit, const Sink &sink,
                      Control &control, const std::string &etag);
Response get(const std::string &url, Purpose purpose, std::uint64_t limit, const Sink &sink,
             Control &control, const std::string &etag = {});
Response fetch(const std::string &url, Purpose purpose, std::size_t limit, std::string &body,
               Control &control, const std::string &etag = {});
} // namespace store::net
