// ProsperoStore - Background catalog work and nonblocking frame delivery.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/client.hpp"
#include "core/image.hpp"
#include "install/transaction.hpp"
#include "system/inventory.hpp"
#include <deque>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <vector>

namespace store
{
struct Update
{
    enum class Kind
    {
        catalog,
        detail,
        icon,
        qr,
        inventory,
        notice, // message is its title, detail its body
        job,    // a finished install, update or uninstall: entry.id, ok, message, detail
        error
    } kind = Kind::error;
    std::string detail;
    catalog::Snapshot snapshot;
    catalog::Entry entry;
    hui::Image image;
    system::Inventory installed;
    std::uint64_t generation = 0;
    std::string message;
    bool ok = false;
};
// What the installer is doing, for the frame that draws it.
struct JobView
{
    std::string id; // the title being changed; empty when idle
    install::Phase phase = install::Phase::idle;
    std::uint64_t done = 0, total = 0;
    std::vector<std::string> waiting; // titles queued behind it
};
class Service
{
  public:
    // version is the running store's contentVersion; empty skips its update check.
    explicit Service(std::string root, std::string version = {})
        : root_(std::move(root)), version_(std::move(version))
    {
    }
    ~Service();
    // Set before start() to run the installer: one transaction at a time, on
    // its own worker, after recovering whatever the journal says was interrupted.
    bool installer = false;
    // Tests only: replaces the environment built from the console's configuration.
    std::optional<install::Environment> installer_environment;
    bool start();
    void stop();
    bool take(std::vector<Update> &updates);
    bool request_detail(const std::string &id);
    bool request_icons(const std::vector<std::string> &ids);
    void report_frames(std::string report);
    // Never block: false means "ask again next frame" (or the queue is full).
    // location is the scanned folder that holds, or will hold, the app's folder.
    bool request_install(const catalog::Entry &entry, const std::string &location);
    bool request_uninstall(const std::string &id, const std::string &location);
    bool cancel_job(const std::string &id);
    bool job(JobView &view);
    // The titles running now, refreshed every two seconds by the installer's
    // worker. known is false while the console's sandbox folder can't be listed.
    bool running(std::vector<std::string> &ids, bool &known);

  private:
    struct Job
    {
        bool uninstall = false;
        catalog::Entry entry;
        std::string location;
    };
    static void *entry(void *self);
    static void *icon_entry(void *self);
    static void *install_entry(void *self);
    void load_icons();
    void run();
    void run_installer();
    void publish(Update update);
    void check_store_update();
    bool load_policy(system::ScanPolicy &policy) const;
    std::string root_, version_;
    net::Control control_;
    net::Control icon_control_;
    net::Control job_control_;
    pthread_t thread_{};
    pthread_t icon_thread_{};
    pthread_t install_thread_{};
    bool started_ = false;
    bool icons_started_ = false;
    bool installer_started_ = false;
    std::mutex mutex_;
    std::vector<Update> updates_;
    std::string detail_;
    std::vector<std::string> icons_;
    std::vector<catalog::Entry> entries_;
    std::map<std::string, std::string> versions_;
    std::deque<Job> jobs_;
    std::string job_id_;
    std::vector<std::string> running_;
    bool running_known_ = false;
    install::Progress progress_;
    std::uint64_t generation_ = 0;
    bool online_ = false;
    std::string frame_report_;
};
} // namespace store
