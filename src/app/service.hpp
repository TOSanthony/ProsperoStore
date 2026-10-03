// ProsperoStore - Background catalog work and nonblocking frame delivery.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/client.hpp"
#include "core/image.hpp"
#include "system/inventory.hpp"
#include <mutex>
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
        error
    } kind = Kind::error;
    catalog::Snapshot snapshot;
    catalog::Entry entry;
    hui::Image image;
    system::Inventory installed;
    std::uint64_t generation = 0;
    std::string message;
};
class Service
{
  public:
    explicit Service(std::string root) : root_(std::move(root))
    {
    }
    ~Service();
    bool start();
    void stop();
    bool take(std::vector<Update> &updates);
    bool request_detail(const std::string &id);
    bool request_icons(const std::vector<std::string> &ids);
    void report_frames(std::string report);

  private:
    static void *entry(void *self);
    static void *icon_entry(void *self);
    void load_icons();
    void run();
    void publish(Update update);
    std::string root_;
    net::Control control_;
    net::Control icon_control_;
    pthread_t thread_{};
    pthread_t icon_thread_{};
    bool started_ = false;
    bool icons_started_ = false;
    std::mutex mutex_;
    std::vector<Update> updates_;
    std::string detail_;
    std::vector<std::string> icons_;
    std::vector<catalog::Entry> entries_;
    std::uint64_t generation_ = 0;
    bool online_ = false;
    std::string frame_report_;
};
} // namespace store
