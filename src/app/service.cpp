// ProsperoStore - Catalog cache and HTTPS never run on the render thread.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/service.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include "system/locations.hpp"
#include "system/storage_probe.hpp"

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
    return started_;
}
void Service::stop()
{
    control_.cancel();
    if (started_)
        pthread_join(thread_, nullptr);
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
void Service::publish(Update update)
{
    std::lock_guard lock(mutex_);
    // At most the startup snapshots plus one outstanding detail are in flight.
    if (updates_.size() >= 8)
        updates_.erase(updates_.begin());
    updates_.push_back(std::move(update));
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
#ifdef STORE_DEVELOPMENT
    if (root_ == "/data/prosperostore")
    {
        std::string configuration, manual, policy_error;
        const bool configured =
            hui::save::read_file("/data/shadowmount/config.ini", &configuration, 256 * 1024);
        const bool listed =
            hui::save::read_file("/data/shadowmount/manual.lst", &manual, 256 * 1024);
        system::ScanPolicy policy;
        if (!system::scan_policy(configuration, manual, policy, policy_error))
            hui::sys::log("[STORE] storage policy error=%s", policy_error.c_str());
        else
        {
            hui::sys::log("[STORE] storage config=%d manual=%d roots=%zu entries=%zu depth=%u",
                          configured, listed, policy.roots.size(), policy.manual.size(),
                          policy.depth);
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
        }
    }
#endif
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
                result.kind = Update::Kind::detail;
            publish(std::move(result));
        }
        hui::sys::sleep_us(100000);
    }
}
} // namespace store
