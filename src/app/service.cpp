// ProsperoStore - Catalog cache and HTTPS never run on the render thread.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/service.hpp"
#include "core/save_file.hpp"
#include "core/qr.hpp"
#include "platform/ps5/system.hpp"
#include "system/locations.hpp"
#include "system/storage_probe.hpp"
#include "catalog/icons.hpp"
#include "../../examples/update-check/update_check.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <cerrno>
#include <sys/stat.h>

namespace store
{
Service::~Service()
{
    stop();
}
bool Service::start()
{
    if (started_)
        return true;
    started_ = pthread_create(&thread_, nullptr, entry, this) == 0;
    if (started_)
        icons_started_ = pthread_create(&icon_thread_, nullptr, icon_entry, this) == 0;
    if (started_ && !icons_started_)
        stop();
    return started_;
}
void Service::stop()
{
    control_.cancel();
    icon_control_.cancel();
    if (started_)
        pthread_join(thread_, nullptr);
    if (icons_started_)
        pthread_join(icon_thread_, nullptr);
    control_.connection.reset();
    icon_control_.connection.reset();
    icons_started_ = false;
    started_ = false;
}
bool Service::take(std::vector<Update> &updates)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || updates_.empty())
        return false;
    updates.swap(updates_);
    return true;
}
bool Service::request_detail(const std::string &id)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    detail_ = id;
    return true;
}
bool Service::request_icons(const std::vector<std::string> &ids)
{
    if (ids.size() > 16)
        return false;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    icons_ = ids;
    return true;
}
void Service::publish(Update update)
{
    // Backpressure stays on workers; never drop an image the screen is waiting for.
    while (!control_.cancelled.load())
    {
        {
            std::lock_guard lock(mutex_);
            if (updates_.size() < 8)
            {
                if (update.kind == Update::Kind::catalog)
                {
                    entries_ = update.snapshot.entries;
                    update.generation = ++generation_;
                    online_ = update.snapshot.online;
                }
                updates_.push_back(std::move(update));
                return;
            }
        }
        hui::sys::sleep_us(10000);
    }
}
void Service::report_frames(std::string report)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock())
        frame_report_ = std::move(report);
}
void *Service::entry(void *self)
{
    static_cast<Service *>(self)->run();
    return nullptr;
}
void Service::run()
{
#ifdef STORE_SANDBOX_CONTROL
    std::string probe_body;
    const auto probe =
        net::fetch("https://homebrew.page/api/v1/manifest.json", net::Purpose::catalog,
                   catalog::kVersionsLimit, probe_body, control_);
    hui::sys::log("[STORE] sandbox TLS control status=%d bytes=%zu error=%s", probe.status,
                  probe_body.size(), probe.error.c_str());
#endif
    if (!root_.empty() && !hui::save::ensure_directory(root_))
    {
        Update failure;
        failure.message = "Storage is unavailable. The catalog could not be loaded.";
        publish(std::move(failure));
        return;
    }
    catalog::Client client(root_.empty() ? "" : root_ + "/cache");
    system::ScanPolicy scan_policy;
    bool can_scan = false;
    if (root_ == "/data/prosperostore")
    {
        std::string configuration, manual, policy_error;
        const bool configured =
            hui::save::read_file("/data/shadowmount/config.ini", &configuration, 256 * 1024);
        const bool listed =
            hui::save::read_file("/data/shadowmount/manual.lst", &manual, 256 * 1024);
        const auto unreadable = [](const char *path, bool read)
        {
            struct stat info
            {
            };
            return !read && (lstat(path, &info) == 0 || errno != ENOENT);
        };
        system::ScanPolicy policy;
        if (unreadable("/data/shadowmount/config.ini", configured) ||
            unreadable("/data/shadowmount/manual.lst", listed))
            hui::sys::log("[STORE] storage policy error=Existing configuration is unreadable; "
                          "refusing default paths");
        else if (!system::scan_policy(configuration, manual, policy, policy_error))
            hui::sys::log("[STORE] storage policy error=%s", policy_error.c_str());
        else
        {
            scan_policy = policy;
            can_scan = true;
            hui::sys::log("[STORE] storage config=%d manual=%d roots=%zu entries=%zu depth=%u",
                          configured, listed, policy.roots.size(), policy.manual.size(),
                          policy.depth);
#ifdef STORE_DEVELOPMENT
            for (const auto &path : policy.roots)
            {
                if (control_.cancelled.load())
                    break;
                const auto drive = system::drive_root(path);
                if (drive.empty())
                    continue;
                const auto probe = system::probe_storage(path);
                const bool work_safe = system::work_path_unscanned(
                    policy, drive + "/prosperostore/staging/transaction/PPSA99000");
                hui::sys::log(
                    "[STORE] storage path=%s fs=%s available=%llu rename=%d work-safe=%d error=%s",
                    path.c_str(), probe.filesystem.c_str(),
                    static_cast<unsigned long long>(probe.available), probe.renamed, work_safe,
                    probe.error.c_str());
            }
#endif
        }
    }
    catalog::Snapshot snapshot;
    std::string error;
    if (client.cached(snapshot, error))
    {
        Update cached;
        cached.kind = Update::Kind::catalog;
        cached.snapshot = snapshot;
        cached.message = "Offline catalog • Checking for updates";
        publish(std::move(cached));
    }
    bool refreshed = false;
    for (unsigned attempt = 0; attempt < 4 && !control_.cancelled.load(); ++attempt)
    {
        if (client.refresh(snapshot, control_, error))
        {
            refreshed = true;
            break;
        }
        hui::sys::log("[STORE] catalog attempt=%u error=%s", attempt + 1, error.c_str());
        if (attempt < 3)
            for (unsigned tick = 0; tick < (10U << attempt) && !control_.cancelled.load(); ++tick)
                hui::sys::sleep_us(100000);
    }
    if (!control_.cancelled.load())
    {
        Update result;
        result.kind = snapshot.verified ? Update::Kind::catalog : Update::Kind::error;
        result.snapshot = snapshot;
        result.message = refreshed ? "Catalog verified • Up to date" : "Offline • " + error;
        hui::sys::log("[STORE] catalog verified=%d online=%d sequence=%llu apps=%zu",
                      snapshot.verified, refreshed,
                      static_cast<unsigned long long>(snapshot.manifest.sequence),
                      snapshot.entries.size());
        publish(std::move(result));
    }
    if (refreshed && !control_.cancelled.load())
        check_store_update();
    if (!control_.cancelled.load())
    {
        Update installed;
        installed.kind = Update::Kind::inventory;
        if (can_scan)
            installed.installed =
                system::scan_installed(scan_policy, root_ + "/receipts", control_.cancelled);
        else
        {
            installed.installed.complete = false;
            installed.installed.errors.push_back("Installed apps could not be inspected with the "
                                                 "current permissions or scan configuration.");
        }
        hui::sys::log("[STORE] inventory complete=%d apps=%zu errors=%zu",
                      installed.installed.complete, installed.installed.apps.size(),
                      installed.installed.errors.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(3, installed.installed.errors.size());
             ++i)
            hui::sys::log("[STORE] inventory error=%s", installed.installed.errors[i].c_str());
        publish(std::move(installed));
    }
    while (!control_.cancelled.load())
    {
        std::string id;
        std::string frame_report;
        {
            std::lock_guard lock(mutex_);
            id.swap(detail_);
            frame_report.swap(frame_report_);
        }
        if (!frame_report.empty())
            hui::sys::log("[STORE] %s", frame_report.c_str());
        if (!id.empty())
        {
            Update result;
            if (client.detail(snapshot, id, result.entry, control_, result.message))
            {
                result.kind = Update::Kind::detail;
                if (!result.entry.large_icon.empty())
                {
                    auto artwork_entry = result.entry;
                    artwork_entry.icon = artwork_entry.large_icon;
                    catalog::Icons artwork(root_.empty() ? "" : root_ + "/cache/icons");
                    if (!artwork.cached(artwork_entry, result.image) && snapshot.online)
                    {
                        std::string encoded;
                        const auto response = net::fetch(artwork_entry.icon, net::Purpose::catalog,
                                                         2u << 20, encoded, control_);
                        if (response.ok())
                            artwork.store(artwork_entry, encoded, result.image);
                    }
                }
            }
            else
                result.entry.id = id;
            const bool verified_detail = result.kind == Update::Kind::detail;
            publish(std::move(result));
            if (verified_detail)
            {
                Update qr;
                qr.kind = Update::Kind::qr;
                qr.entry.id = id;
                if (hui::encode_qr("https://homebrew.page/app/" + id + "/", qr.image))
                    publish(std::move(qr));
            }
        }
        hui::sys::sleep_us(100000);
    }
}

namespace
{
// The update check's transport has no context argument: one check at a time.
net::Control *check_control = nullptr;
int check_fetch(const char *url, const char *, char *body, std::size_t capacity,
                std::size_t *length, int *http_status)
{
    std::string data;
    const auto response = net::fetch(url, net::Purpose::catalog, capacity, data, *check_control);
    *http_status = response.status;
    *length = 0;
    if (response.error == "The response exceeds its size limit")
        return UPDATE_CHECK_FETCH_TOO_LARGE;
    if (response.status == 0)
        return -1;
    if (data.size() > capacity)
        return UPDATE_CHECK_FETCH_TOO_LARGE;
    std::memcpy(body, data.data(), data.size());
    *length = data.size();
    return 0;
}
} // namespace

// Once per launch, after the catalog: is a newer ProsperoStore listed? Any
// failure means "unknown", and unknown shows nothing.
void Service::check_store_update()
{
    if (version_.empty())
        return;
    update_check_result result{};
    check_control = &control_;
    update_check_run_with(check_fetch, "PPSA99000", version_.c_str(), &result);
    check_control = nullptr;
    hui::sys::log("[STORE] update check installed=%s state=%d reason=%s available=%s",
                  version_.c_str(), static_cast<int>(result.state),
                  update_check_reason_text(result.reason), result.available);
    if (result.state != UPDATE_CHECK_AVAILABLE)
        return;
    Update notice;
    notice.kind = Update::Kind::notice;
    notice.message = std::string("Update available: ") + result.version;
    notice.detail = "A newer ProsperoStore is listed on homebrew.page.";
    publish(std::move(notice));
}

void *Service::icon_entry(void *self)
{
    static_cast<Service *>(self)->load_icons();
    return nullptr;
}

void Service::load_icons()
{
    catalog::Icons artwork(root_.empty() ? "" : root_ + "/cache/icons");
    std::set<std::string> failed_icons;
    std::uint64_t failure_generation = 0;
    while (!icon_control_.cancelled.load())
    {
        catalog::Entry entry;
        std::uint64_t generation = 0;
        bool online = false;
        {
            std::lock_guard lock(mutex_);
            generation = generation_;
            if (!icons_.empty())
            {
                const auto found =
                    std::find_if(entries_.begin(), entries_.end(),
                                 [&](const auto &item) { return item.id == icons_.front(); });
                if (found != entries_.end())
                    entry = *found;
                icons_.erase(icons_.begin());
                online = online_;
            }
        }
        if (generation != failure_generation)
        {
            failed_icons.clear();
            failure_generation = generation;
        }
        const auto key = catalog::Icons::key(entry);
        if (!key.empty() && !failed_icons.contains(key))
        {
            Update result;
            result.kind = Update::Kind::icon;
            result.entry.id = entry.id;
            result.generation = generation;
#ifdef STORE_DEVELOPMENT
            const auto started = hui::sys::monotonic_us();
#endif
            bool loaded = artwork.cached(entry, result.image);
#ifdef STORE_DEVELOPMENT
            const bool cached = loaded;
#endif
            if (!loaded && online)
            {
                std::string encoded;
                const auto response =
                    net::fetch(entry.icon, net::Purpose::catalog, 2u << 20, encoded, icon_control_);
                loaded = response.ok() && artwork.store(entry, encoded, result.image);
            }
#ifdef STORE_DEVELOPMENT
            hui::sys::log(
                "[STORE] icon id=%s cached=%d loaded=%d elapsed_ms=%llu", entry.id.c_str(), cached,
                loaded,
                static_cast<unsigned long long>((hui::sys::monotonic_us() - started) / 1000));
#endif
            if (loaded)
                publish(std::move(result));
            else if (online)
                failed_icons.insert(key);
        }
        if (entry.id.empty())
            hui::sys::sleep_us(10000);
    }
}
} // namespace store
