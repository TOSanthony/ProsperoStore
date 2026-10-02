// ProsperoStore - Background catalog work and nonblocking frame delivery.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/client.hpp"
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
        error
    } kind = Kind::error;
    catalog::Snapshot snapshot;
    catalog::Entry entry;
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
    void report_frames(std::string report);

  private:
    static void *entry(void *self);
    void run();
    void publish(Update update);
    std::string root_;
    net::Control control_;
    pthread_t thread_{};
    bool started_ = false;
    std::mutex mutex_;
    std::vector<Update> updates_;
    std::string detail_;
    std::string frame_report_;
};
} // namespace store
