// ProsperoStore - Native display, input, audio and clean application lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "app/service.hpp"
#include "audio/cues.hpp"
#include "core/frame_stats.hpp"
#include "core/image.hpp"
#include "core/version.hpp"
#include "core/save_file.hpp"
#include "diag/diagnostics.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/ime.hpp"
#include "platform/ps5/system.hpp"
#include "../examples/sandbox-elevation/elevation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <cstdlib>
#include <map>
#include <mutex>
#include <GL/glcorearb.h>

namespace
{
std::atomic<bool> quit{false};
std::string request_path;
std::string handled_path;
std::string run_token;
// Requests of a scripted run, handed from the reader thread to the frame loop.
std::mutex remote_guard;
std::vector<std::pair<std::string, std::string>> remote_requests;

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
                handled != token && hui::save::write_atomic(handled_path, token).empty())
            {
                if (std::strcmp(verb, "quit") == 0)
                {
                    hui::sys::log("[STORE] remote clean exit token=%s", token);
                    quit.store(true);
                }
                else
                {
                    hui::sys::log("[STORE] remote request %s %s token=%s", verb, argument, token);
                    std::lock_guard lock(remote_guard);
                    remote_requests.emplace_back(verb, argument);
                }
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
    constexpr const char *storage_root = "/data/prosperostore";
    const bool elevated = elevation_status == elevation::Status::ok;
    const std::string app_root =
        elevation_status == elevation::Status::ok ? "/mnt/sandbox/PPSA99000_000/app0" : "/app0";
    request_path = std::string(storage_root) + "/dev/request.txt";
    handled_path = std::string(storage_root) + "/handled.txt";
    if (elevated && !store::diag::start(storage_root))
        sys::log("[STORE] persistent diagnostics unavailable");
#ifdef STORE_DEVELOPMENT
    if (elevated)
        hui::save::read_file(std::string(storage_root) + "/dev/run.txt", &run_token, 64);
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
        elevated && pthread_create(&request_thread, nullptr, development_requests, nullptr) == 0;
    store::Screen screen;
    // The picture for coming-soon apps that have no artwork of their own.
    std::uint32_t coming_soon_texture = 0;
    {
        std::string encoded;
        Image image;
        if (save::read_file(app_root + "/assets/images/coming-soon.png", &encoded, 2u << 20) &&
            decode_png(encoded, image))
            coming_soon_texture =
                renderer.batch().create_texture(image.width, image.height, image.rgba.data());
        screen.set_coming_soon_art(coming_soon_texture);
    }
    store::Service service(elevated ? storage_root : "",
                           read_content_version(app_root + "/sce_sys/param.json"));
#ifdef STORE_INSTALLER
    service.installer = elevated;
    const char *installer_reason = "This console didn't grant permission to write, so nothing "
                                   "can be installed.";
#else
    const char *installer_reason = "Installing is not switched on in this build.";
#endif
    screen.set_installer(service.installer, false, installer_reason, "/data/homebrew");
    if (!service.start())
        screen.set_status("The catalog service could not start");
    if (elevation_status != elevation::Status::ok)
        screen.set_catalog({}, "Read only: install permission unavailable");
    InputTracker input;
    FrameStats stats;
    std::int64_t previous = sys::monotonic_us();
    sys::log("[STORE] interactive width=%d height=%d", display.width(), display.height());
    bool first_swap = true;
    // Pictures are uploaded once and kept for the session. Creating and
    // deleting textures while the focus moves stalled frames for over a
    // second on the console, so nothing is deleted on the way.
    struct Art
    {
        std::uint32_t icon = 0, large = 0, code = 0;
        int code_width = 0;
        std::string hash;
        std::uint64_t seen = 0; // the last frame it was on screen
    };
    std::map<std::string, Art> textures;
    std::map<std::string, std::string> hashes; // the current catalog's icon hashes
    const auto release = [](Art &art)
    {
        for (auto *texture : {&art.icon, &art.large, &art.code})
            if (*texture)
                glDeleteTextures(1, texture);
        art = {};
    };
    // A catalog of a thousand apps must not hold a thousand pictures: each
    // kind has a budget, and past it the picture that has been off screen
    // longest gives its texture to the new one. Giving a texture new pixels
    // costs about a millisecond on the console; nothing is created or deleted.
    std::size_t icon_budget = 160; // 256 x 256: about 42 MB
    constexpr std::size_t kLargeBudget = 12, kCodeBudget = 12;
    std::uint64_t frame_number = 0;
    const auto place = [&](std::uint32_t Art::*slot, std::size_t budget, const std::string &id,
                           const Image &image) -> std::uint32_t
    {
        std::size_t used = 0;
        Art *victim = nullptr;
        const std::string *victim_id = nullptr;
        for (auto &[other, art] : textures)
            if (art.*slot)
            {
                ++used;
                if (other != id && art.seen + 120 < frame_number &&
                    (!victim || art.seen < victim->seen))
                {
                    victim = &art;
                    victim_id = &other;
                }
            }
        if (used < budget || !victim)
            return renderer.batch().create_texture(image.width, image.height, image.rgba.data());
        const std::uint32_t texture = victim->*slot;
        victim->*slot = 0;
        if (slot == &Art::icon)
            screen.set_icon(*victim_id, 0);
        else if (slot == &Art::large)
            screen.set_art(*victim_id, 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba.data());
        return texture;
    };
    std::vector<std::string> requested_icons;
    std::uint64_t catalog_generation = 0;
    std::vector<store::Update> updates;
    [[maybe_unused]] const char *note = "", *previous_note = "";
    [[maybe_unused]] std::int64_t tour_until = 0, tour_next = 0;
    [[maybe_unused]] unsigned tour_step = 0;
    [[maybe_unused]] int bench_step = -1;
    ps5::Ime keyboard;
    bool keyboard_active = false;
    while (!quit.load() && !screen.wants_quit())
    {
        const auto now = sys::monotonic_us();
        const double ms = static_cast<double>(now - previous) / 1000.0;
        previous = now;
        const float dt = std::clamp(static_cast<float>(ms / 1000.0), 0.001f, 0.1f);
        std::array<PadSample, 64> samples{};
        const auto count = pad.read(samples);
        ui::Feedback feedback;
        if (updates.empty())
            service.take(updates);
        // At most one texture upload per frame, including after a render stall.
        if (!updates.empty())
        {
            auto update = std::move(updates.front());
            updates.erase(updates.begin());
            note = update.kind == store::Update::Kind::catalog     ? "catalog"
                   : update.kind == store::Update::Kind::icon      ? "icon"
                   : update.kind == store::Update::Kind::detail    ? "detail"
                   : update.kind == store::Update::Kind::qr        ? "code"
                   : update.kind == store::Update::Kind::inventory ? "inventory"
                                                                   : "update";
            if (update.kind == store::Update::Kind::catalog)
            {
                requested_icons.clear();
                catalog_generation = update.generation;
                hashes.clear();
                for (const auto &entry : update.snapshot.entries)
                    if (!entry.icon.empty())
                        hashes[entry.id] = entry.icon_hash;
                // Only a picture that changed, or whose app left the catalog, goes.
                for (auto it = textures.begin(); it != textures.end();)
                {
                    const auto listed = hashes.find(it->first);
                    if (listed == hashes.end() || listed->second != it->second.hash)
                    {
                        release(it->second);
                        it = textures.erase(it);
                    }
                    else
                        ++it;
                }
                std::vector<store::App> apps;
                for (const auto &entry : update.snapshot.entries)
                {
                    apps.push_back({entry.id, entry.name, entry.author, entry.description,
                                    entry.kind, entry.version,
                                    entry.status == "coming_soon" ? "Coming soon" : "", 0,
                                    entry.released, entry.updated});
                    apps.back().available_version = entry.content_version;
                }
                screen.set_catalog(std::move(apps),
                                   elevated ? update.message
                                            : "Read only: install permission unavailable • " +
                                                  update.message,
                                   update.snapshot.online);
                for (const auto &[id, art] : textures)
                {
                    screen.set_icon(id, art.icon);
                    screen.set_art(id, art.large);
                }
            }
            else if (update.kind == store::Update::Kind::inventory)
                screen.set_inventory(std::move(update.installed));
            else if (update.kind == store::Update::Kind::notice)
                screen.notify(std::move(update.message), std::move(update.detail));
            else if (update.kind == store::Update::Kind::job)
                screen.finish_job(update.ok, std::move(update.message), std::move(update.detail));
            else if (update.kind == store::Update::Kind::icon)
            {
                const auto listed = hashes.find(update.entry.id);
                if (update.generation == catalog_generation && listed != hashes.end() &&
                    !textures[update.entry.id].icon)
                {
                    auto &art = textures[update.entry.id];
                    art.hash = listed->second;
                    art.seen = frame_number;
                    art.icon = place(&Art::icon, icon_budget, update.entry.id, update.image);
                    screen.set_icon(update.entry.id, art.icon);
                }
            }
            else if (update.kind == store::Update::Kind::detail)
            {
                screen.set_detail(update.entry);
                const auto listed = hashes.find(update.entry.id);
                if (!update.image.rgba.empty() && listed != hashes.end() &&
                    !textures[update.entry.id].large)
                {
                    auto &art = textures[update.entry.id];
                    art.hash = listed->second;
                    art.seen = frame_number;
                    art.large = place(&Art::large, kLargeBudget, update.entry.id, update.image);
                    screen.set_art(update.entry.id, art.large);
                }
            }
            else if (update.kind == store::Update::Kind::qr)
            {
                // An app's code never changes: made the first time its page opens.
                auto &art = textures[update.entry.id];
                if (!art.code)
                {
                    art.seen = frame_number;
                    art.code = place(&Art::code, kCodeBudget, update.entry.id, update.image);
                    art.code_width = update.image.width;
                    if (const auto listed = hashes.find(update.entry.id); listed != hashes.end())
                        art.hash = listed->second;
                }
                screen.set_qr(update.entry.id, art.code, art.code_width);
            }
            else if (!update.entry.id.empty())
                screen.set_detail_error(update.entry.id, update.message);
            else
                screen.catalog_failed(update.message);
        }
        if (!screen.pending_detail.empty() && service.request_detail(screen.pending_detail))
            screen.pending_detail.clear();
        // The page's request goes to the installer; one refused by a busy lock is asked again.
        if (auto &order = screen.pending_order; order.kind != store::Order::Kind::none)
        {
            const bool accepted = order.kind == store::Order::Kind::install
                                      ? service.request_install(order.entry, order.location)
                                  : order.kind == store::Order::Kind::uninstall
                                      ? service.request_uninstall(order.id, order.location)
                                      : service.cancel_job(order.id);
            if (accepted || !service.installer)
                order = {};
        }
        {
            std::vector<std::string> running;
            bool known = false;
            if (service.running(running, known))
                screen.set_running(std::move(running), known);
        }
        if (store::JobView view; service.job(view))
            screen.set_activity({view.id, static_cast<int>(view.phase), view.done, view.total,
                                 std::move(view.waiting)});
        auto frame = input.update(std::span(samples.data(), count), now);
#ifdef STORE_DEVELOPMENT
        // A scripted run: requests become what a player would do, and a tour
        // moves the focus the way a hand on the D-pad does.
        {
            std::pair<std::string, std::string> request;
            {
                std::unique_lock lock(remote_guard, std::try_to_lock);
                if (lock.owns_lock() && !remote_requests.empty())
                {
                    request = std::move(remote_requests.front());
                    remote_requests.erase(remote_requests.begin());
                }
            }
            const auto &[verb, argument] = request;
            if (verb == "open")
                sys::log("[STORE] remote open %s found=%d", argument.c_str(),
                         screen.open_app(argument));
            else if (verb == "install")
                screen.remote_install(argument);
            else if (verb == "uninstall")
                sys::log("[STORE] remote uninstall %s accepted=%d", argument.c_str(),
                         screen.remote_uninstall(argument));
            else if (verb == "order")
                sys::log("[STORE] remote order sent=%d", screen.remote_order());
            else if (verb == "cancel")
            {
                screen.pending_order = {};
                screen.pending_order.kind = store::Order::Kind::cancel;
                screen.pending_order.id = argument;
            }
            else if (verb == "tour")
            {
                tour_until = now + std::atoll(argument.c_str()) * 1000000;
                tour_step = 0;
                tour_next = now;
            }
            else if (verb == "texbench")
                bench_step = 0;
            else if (verb == "stress")
                screen.stress(static_cast<std::size_t>(std::atoll(argument.c_str())));
            else if (verb == "pool")
                icon_budget = static_cast<std::size_t>(std::max(4LL, std::atoll(argument.c_str())));
        }
        if (now < tour_until && now >= tour_next)
        {
            // Across a row, down, back across, down; a page opened and closed
            // now and then; back to the top when the grid ends.
            static constexpr char kPath[] = "rrrrdlllldrrrrXBdllllduuuuu";
            // A tour only looks: Cross on an open page would install the app.
            char step = kPath[tour_step++ % (sizeof(kPath) - 1)];
            if (screen.page_open())
                step = 'B';
            frame = {};
            if (step == 'X')
                frame.pressed = action_bit(Action::confirm);
            else if (step == 'B')
                frame.pressed = action_bit(Action::back);
            else
                frame.nav = step == 'r'   ? Direction::right
                            : step == 'l' ? Direction::left
                            : step == 'd' ? Direction::down
                                          : Direction::up;
            tour_next = now + (step == 'X' ? 1500000 : 170000);
            note = "tour";
        }
        if (bench_step >= 0)
        {
            // What does a texture cost on the frame? Eight of each, one per frame.
            static std::vector<std::uint8_t> pixels(256 * 256 * 4, 0x80);
            static std::uint32_t made[8]{};
            const auto started = sys::monotonic_us();
            const int index = bench_step % 8;
            const char *what = "create";
            if (bench_step < 8)
                made[index] = renderer.batch().create_texture(256, 256, pixels.data());
            else if (bench_step < 16)
            {
                what = "update";
                glBindTexture(GL_TEXTURE_2D, made[index]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE,
                                pixels.data());
            }
            else
            {
                what = "delete";
                glDeleteTextures(1, &made[index]);
            }
            sys::log("[STORE] texbench %s %d call_us=%lld", what, index,
                     static_cast<long long>(sys::monotonic_us() - started));
            note = what;
            bench_step = bench_step == 23 ? -1 : bench_step + 1;
        }
#endif
        const bool keyboard_owns_input = keyboard_active || screen.pending_search;
        if (screen.pending_search && !frame.is_held(Action::north))
        {
            screen.pending_search = false;
            keyboard_active =
                keyboard.open("Search ProsperoStore", "App name or developer", screen.query());
            if (!keyboard_active)
            {
                screen.set_status("The system keyboard could not open. Try Search again.");
                feedback.play(audio::Cue::error);
            }
        }
        if (keyboard_active)
        {
            const auto state = keyboard.poll();
            if (state == ps5::Ime::State::accepted)
                screen.set_query(keyboard.text());
            else if (state == ps5::Ime::State::failed)
                screen.set_status("The system keyboard closed unexpectedly. Try Search again.");
            keyboard_active = state == ps5::Ime::State::open;
        }
        screen.update(keyboard_owns_input ? InputFrame{} : frame, dt, feedback);
        // What is on screen first; then, when the whole catalog fits the
        // budget, the rest of it, sixteen at a time.
        ++frame_number;
        const auto on_screen = screen.artwork();
        for (const auto &id : on_screen)
            if (const auto found = textures.find(id); found != textures.end())
                found->second.seen = frame_number;
        std::vector<std::string> missing;
        for (const auto &id : hashes.size() <= icon_budget ? screen.artwork_backlog() : on_screen)
        {
            const auto found = textures.find(id);
            if (missing.size() < 16 && hashes.contains(id) &&
                (found == textures.end() || !found->second.icon))
                missing.push_back(id);
        }
        if (missing != requested_icons && service.request_icons(missing))
            requested_icons = std::move(missing);
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
            // The console's splash picture stays up until there is a frame to show.
            sys::hide_splash_screen();
            sys::log("[STORE] first-swap ok");
            first_swap = false;
        }
        stats.add(ms);
#ifdef STORE_DEVELOPMENT
        // A late frame is named with what the frame before it did.
        if (ms > 25.0 && !first_swap)
            sys::log("[STORE] hitch ms=%.1f after=%s", ms, previous_note);
        previous_note = note;
        note = "";
#endif
        if (stats.count() == 600)
        {
            char report[256]{};
            stats.format(report, sizeof(report));
            service.report_frames(std::string(report) +
                                  " icons=" + std::to_string(textures.size()));
            stats.reset();
        }
    }
    quit.store(true);
    keyboard.close();
    service.stop();
    store::net::stop_transport();
    if (requests_started)
        pthread_join(request_thread, nullptr);
    audio.stop();
    pad.close();
    if (coming_soon_texture)
        glDeleteTextures(1, &coming_soon_texture);
    for (auto &[id, art] : textures)
    {
        (void)id;
        release(art);
    }
    renderer.release();
    display.close();
    sys::log("[STORE] teardown complete");
#ifdef STORE_DEVELOPMENT
    sys::log("[STORE] run end token=%s", run_token.c_str());
#endif
    store::diag::stop();
    sys::quit();
}
