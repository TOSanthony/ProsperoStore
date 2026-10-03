// ProsperoStore - ZIP artifacts: validated from the directory, then unpacked.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace store::install
{
constexpr std::size_t kArchiveEntries = 100000;
constexpr std::size_t kArchivePath = 512;

struct ArchiveInfo
{
    std::uint64_t unpacked = 0; // What the directory declares; enforced while unpacking.
    std::size_t files = 0;
};
// Reads the archive's directory only. Accepts one top-level folder named after
// the title, holding sce_sys/param.json; refuses absolute and parent paths,
// links and special files, encryption, duplicates and anything over the limits.
bool inspect_archive(const std::string &path, std::string_view title, ArchiveInfo &out,
                     std::string &error);
// Unpacks the title's folder as destination, which must not exist. On failure
// the caller removes destination. written counts unpacked bytes for progress.
bool extract_archive(const std::string &path, std::string_view title,
                     const std::string &destination, const std::atomic<bool> &cancelled,
                     std::atomic<std::uint64_t> &written, std::string &error);
} // namespace store::install
