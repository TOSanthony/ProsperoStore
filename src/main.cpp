// ProsperoStore - Native display, input, audio and clean application lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "app/service.hpp"
#include "audio/cues.hpp"
#include "core/frame_stats.hpp"
#include "core/save_file.hpp"
#include "diag/diagnostics.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "../examples/sandbox-elevation/elevation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>

namespace
{
std::atomic<bool> quit{false};
std::string request_path;
std::string handled_path;
std::string run_token;

void *development_requests(void *)
{
#ifdef STORE_DEVELOPMENT
    while (!quit.load())
    {
        std::string request;
        if (hui::save::read_file(request_path, &request, 256))
        {
            char verb[16]{};
            char argument[64]{};
            char token[64]{};
            std::string handled;
            hui::save::read_file(handled_path, &handled, 64);
            if (std::sscanf(request.c_str(), "%15s %63s %63s", verb, argument, token) == 3 &&
                handled != token && std::strcmp(verb, "quit") == 0 &&
                hui::save::write_atomic(handled_path, token).empty())
            {
                hui::sys::log("[STORE] remote clean exit token=%s", token);
                quit.store(true);
            }
        }
        hui::sys::sleep_us(250000);
    }
#endif
    return nullptr;
}
} // namespace

int main()
{
    using namespace hui;
    sys::log("[STORE] PPSA99000 startup");
#ifdef STORE_SANDBOX_CONTROL
    const auto elevation_status = elevation::Status::unavailable;
#else
    const auto elevation_status = elevation::request(elevation::Capability::filesystem);
#endif
    const char *storage_root = elevation_status == elevation::Status::ok
                                   ? "/data/prosperostore"
                                   : "/download0/prosperostore";
    const std::string app_root =
        elevation_status == elevation::Status::ok ? "/mnt/sandbox/PPSA99000_000/app0" : "/app0";
    request_path = app_root + "/dev/request.txt";
    handled_path = std::string(storage_root) + "/handled.txt";
    if (!store::diag::start(storage_root))
        sys::log("[STORE] persistent diagnostics unavailable");
#ifdef STORE_DEVELOPMENT
    hui::save::read_file(app_root + "/dev/run.txt", &run_token, 64);
    sys::log("[STORE] run start token=%s", run_token.c_str());
#endif
    sys::log("[STORE] elevation status=%u", static_cast<unsigned>(elevation_status));
    const int transport = store::net::start_transport(elevation_status == elevation::Status::ok);
    sys::log("[STORE] transport startup rc=0x%08x", static_cast<unsigned>(transport));
    ps5::Display display;
    if (!display.open(3840, 2160))
    {
        sys::log("[STORE] display failed");
        sys::quit();
    }
    gfx::Renderer renderer;
    store::Fonts fonts;
    const bool rendered = renderer.init();
    const bool loaded_fonts = rendered && fonts.load(renderer, app_root + "/assets");
    if (!rendered || !loaded_fonts)
    {
        sys::log("[STORE] initialization failed renderer=%d fonts=%d root=%s", rendered,
                 loaded_fonts, app_root.c_str());
        sys::quit();
    }
    ps5::Pad pad;
    if (!pad.open())
        sys::log("[STORE] controller unavailable");
    audio::Mixer mixer;
    audio::SoundBank sounds;
    const auto bank = sounds.load(app_root + "/assets/audio/sfx");
    sys::log("[STORE] sound files=%d rejected=%d", bank.files, bank.rejected);
    ps5::AudioOut audio;
    audio.start(mixer);
    pthread_t request_thread{};
    const bool requests_started =
        pthread_create(&request_thread, nullptr, development_requests, nullptr) == 0;
    store::Screen screen;
    store::Service service(storage_root);
    if (!service.start())
        screen.set_status("The catalog service could not start");
    if (elevation_status != elevation::Status::ok)
        screen.set_catalog({}, "Read only: install permission unavailable");
    InputTracker input;
    FrameStats stats;
    std::int64_t previous = sys::monotonic_us();
    sys::hide_splash_screen();
    sys::log("[STORE] interactive width=%d height=%d", display.width(), display.height());
    bool first_swap = true;
    while (!quit.load() && !screen.wants_quit())
    {
        const auto now = sys::monotonic_us();
        const double ms = static_cast<double>(now - previous) / 1000.0;
        previous = now;
        const float dt = std::clamp(static_cast<float>(ms / 1000.0), 0.001f, 0.1f);
        std::array<PadSample, 64> samples{};
        const auto count = pad.read(samples);
        ui::Feedback feedback;
        std::vector<store::Update> updates;
        if (service.take(updates))
            for (const auto &update : updates)
            {
                if (update.kind == store::Update::Kind::catalog)
                {
                    std::vector<store::App> apps;
                    for (const auto &entry : update.snapshot.entries)
                        apps.push_back({entry.id, entry.name, entry.author, entry.description,
                                        entry.kind, entry.version,
                                        entry.status == "coming_soon" ? "Coming soon" : "", 0});
                    screen.set_catalog(std::move(apps), update.message);
                }
                else if (update.kind == store::Update::Kind::detail)
                    screen.set_detail(update.entry);
                else
                    screen.set_status(update.message);
            }
        if (!screen.pending_detail.empty() && service.request_detail(screen.pending_detail))
            screen.pending_detail.clear();
        screen.update(input.update(std::span(samples.data(), count), now), dt, feedback);
        for (const auto &cue : feedback.cues)
            sounds.play(mixer, audio::SoundSet::glass, cue);
        if (feedback.rumble_strength > 0)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);
        screen.draw(renderer, fonts.refs);
        renderer.present(0, display.width(), display.height());
        if (!display.swap())
        {
            sys::log("[STORE] presentation failed");
            break;
        }
        if (first_swap)
        {
            sys::log("[STORE] first-swap ok");
            first_swap = false;
        }
        stats.add(ms);
        if (stats.count() == 600)
        {
            char report[256]{};
            stats.format(report, sizeof(report));
            service.report_frames(report);
            stats.reset();
        }
    }
    quit.store(true);
    service.stop();
    store::net::stop_transport();
    if (requests_started)
        pthread_join(request_thread, nullptr);
    audio.stop();
    pad.close();
    renderer.release();
    display.close();
    sys::log("[STORE] teardown complete");
#ifdef STORE_DEVELOPMENT
    sys::log("[STORE] run end token=%s", run_token.c_str());
#endif
    store::diag::stop();
    sys::quit();
}
