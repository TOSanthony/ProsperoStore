// ProsperoStore - Link-refusing file helpers for the installer's work folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>

namespace store::install
{
enum class Kind
{
    absent,
    directory,
    file,
    other, // A symbolic link or special file: never followed, never trusted.
    unknown
};
Kind kind(const std::string &path);
// One level; an existing real directory is accepted.
bool make_directory(const std::string &path);
bool sync_directory(const std::string &path);
// Regular file of at most limit bytes, opened without following a link.
bool read_small(const std::string &path, std::size_t limit, std::string &body);
// Removes a file or a whole folder without following links. True when nothing
// is left at path, including when nothing was there.
bool remove_tree(const std::string &path);
} // namespace store::install
