// ProsperoStore - Artwork remains available during refresh and bounded during stalls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/service.hpp"
#include "catalog/icons.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <set>
#include <thread>

using namespace std::chrono_literals;
namespace
{
std::atomic<bool> refresh_ready{false};
std::atomic<unsigned> downloads{0};
const unsigned char png[] = {
    137, 80, 78, 71, 13, 10, 26,  10,  0,   0,   0, 13, 73, 72, 68, 82, 0,  0,  0,   1,   0,  0,  0,
    1,   8,  6,  0,  0,  0,  31,  21,  196, 137, 0, 0,  0,  11, 73, 68, 65, 84, 120, 156, 99, 96, 0,
    2,   0,  0,  5,  0,  1,  165, 246, 69,  64,  0, 0,  0,  0,  73, 69, 78, 68, 174, 66,  96, 130};
std::string encoded(reinterpret_cast<const char *>(png), sizeof(png));
store::catalog::Snapshot fixture()
{
    store::catalog::Snapshot result;
    result.verified = true;
    result.manifest.sequence = 75;
    for (unsigned i = 0; i < 16; ++i)
    {
        store::catalog::Entry entry;
        entry.id = "PPSA" + std::to_string(99000 + i);
        entry.icon = "https://homebrew.page/icons/" + entry.id + ".png";
        entry.icon_hash = "one";
        result.entries.push_back(entry);
    }
    return result;
}
store::Update next(store::Service &service, store::Update::Kind kind)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::vector<store::Update> updates;
        service.take(updates);
        for (auto &update : updates)
            if (update.kind == kind)
                return std::move(update);
        std::this_thread::sleep_for(5ms);
    }
    assert(false && "worker response timed out");
    return {};
}
} // namespace
namespace store::catalog
{
bool Client::cached(Snapshot &out, std::string &)
{
    out = fixture();
    return true;
}
bool Client::refresh(Snapshot &out, net::Control &control, std::string &)
{
    while (!refresh_ready && !control.cancelled)
        std::this_thread::sleep_for(5ms);
    out = fixture();
    out.online = true;
    return !control.cancelled;
}
bool Client::detail(const Snapshot &, const std::string &id, Entry &entry, net::Control &,
                    std::string &error)
{
    if (id != "PPSA99000")
    {
        error = "offline";
        return false;
    }
    entry = fixture().entries.front();
    entry.large_icon = "https://homebrew.page/full/PPSA99000.png";
    return true;
}
} // namespace store::catalog
namespace store::net
{
void Control::cancel()
{
    cancelled = true;
}
Response fetch(const std::string &, Purpose, std::size_t, std::string &body, Control &,
               const std::string &)
{
    ++downloads;
    body = encoded;
    Response result;
    result.status = 200;
    return result;
}
} // namespace store::net
int main()
{
    char path[] = "/tmp/prospero-service-XXXXXX";
    assert(mkdtemp(path));
    const std::string root = path;
    assert(std::filesystem::create_directory(root + "/cache"));
    store::catalog::Icons cache(root + "/cache/icons");
    hui::Image image;
    assert(cache.store(fixture().entries.front(), encoded, image));
    assert(cache.cached(fixture().entries.front(), image));
    store::Service service(root);
    assert(service.start());
    const auto cached = next(service, store::Update::Kind::catalog);
    assert(cached.generation == 1 && !cached.snapshot.online);
    assert(service.request_icons({"PPSA99000"}));
    const auto icon = next(service, store::Update::Kind::icon);
    assert(icon.generation == 1 && icon.image.width == 1 && downloads == 0);
    refresh_ready = true;
    const auto online = next(service, store::Update::Kind::catalog);
    assert(online.generation == 2 && online.snapshot.online);
    std::vector<std::string> ids;
    for (const auto &entry : fixture().entries)
        ids.push_back(entry.id);
    assert(service.request_icons(ids));
    std::this_thread::sleep_for(1500ms); // Fill the eight-result queue without a render consumer.
    std::set<std::string> received;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (received.size() != ids.size() && std::chrono::steady_clock::now() < deadline)
    {
        std::vector<store::Update> updates;
        service.take(updates);
        assert(updates.size() <= 8);
        for (const auto &update : updates)
            if (update.kind == store::Update::Kind::icon)
            {
                assert(update.generation == 2 && update.image.width == 1);
                received.insert(update.entry.id);
            }
        std::this_thread::sleep_for(5ms);
    }
    assert(received.size() == ids.size() && downloads == 15);
    assert(service.request_detail("PPSA99000"));
    const auto qr = next(service, store::Update::Kind::qr);
    assert(qr.entry.id == "PPSA99000" && qr.image.width >= 116 && downloads == 16);
    assert(service.request_detail("PPSA99000"));
    assert(next(service, store::Update::Kind::detail).image.width == 1 && downloads == 16);
    assert(service.request_detail("PPSA99001"));
    const auto failure = next(service, store::Update::Kind::error);
    assert(failure.entry.id == "PPSA99001" && failure.message == "offline");
    assert(service.request_icons(ids));
    std::this_thread::sleep_for(1500ms);
    const auto before = std::chrono::steady_clock::now();
    service.stop();
    assert(std::chrono::steady_clock::now() - before < 1s);
    std::filesystem::remove_all(root);
}
