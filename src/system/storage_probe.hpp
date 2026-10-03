// ProsperoStore - Non-destructive qualification of a candidate install directory.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
namespace store::system
{
struct StorageProbe
{
    std::string filesystem, error;
    std::uint64_t available = 0;
    bool renamed = false;
};
// Creates only an exclusive, hidden empty test directory and removes that directory.
// No existing title or folder is renamed or deleted. Run on a worker.
StorageProbe probe_storage(const std::string &root);
} // namespace store::system
