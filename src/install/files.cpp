// ProsperoStore - Link-refusing file helpers for the installer's work folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/files.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace store::install
{
namespace
{
bool remove_level(const std::string &path, unsigned depth)
{
    struct stat info
    {
    };
    if (lstat(path.c_str(), &info) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(info.st_mode))
        return unlink(path.c_str()) == 0 || errno == ENOENT;
    if (depth > 300)
        return false;
    // Names are collected first: removing entries while reading a directory
    // may skip some on the console's filesystems.
    std::vector<std::string> names;
    {
        std::unique_ptr<DIR, decltype(&closedir)> directory(opendir(path.c_str()), closedir);
        if (!directory)
            return false;
        for (;;)
        {
            errno = 0;
            const auto *entry = readdir(directory.get());
            if (!entry)
            {
                if (errno)
                    return false;
                break;
            }
            const std::string name = entry->d_name;
            if (name != "." && name != "..")
                names.push_back(name);
        }
    }
    for (const auto &name : names)
        if (!remove_level(path + "/" + name, depth + 1))
            return false;
    return rmdir(path.c_str()) == 0 || errno == ENOENT;
}
} // namespace

Kind kind(const std::string &path)
{
    struct stat info
    {
    };
    if (lstat(path.c_str(), &info) != 0)
        return errno == ENOENT || errno == ENOTDIR ? Kind::absent : Kind::unknown;
    return S_ISDIR(info.st_mode)   ? Kind::directory
           : S_ISREG(info.st_mode) ? Kind::file
                                   : Kind::other;
}

bool make_directory(const std::string &path)
{
    return mkdir(path.c_str(), 0755) == 0 || (errno == EEXIST && kind(path) == Kind::directory);
}

bool sync_directory(const std::string &path)
{
    const int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
    const bool ok = fsync(descriptor) == 0;
    return close(descriptor) == 0 && ok;
}

bool read_small(const std::string &path, std::size_t limit, std::string &body)
{
    const int descriptor = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
        return false;
    struct stat info
    {
    };
    bool ok = fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 &&
              static_cast<std::uint64_t>(info.st_size) <= limit;
    std::string bytes;
    char buffer[4096];
    while (ok)
    {
        const auto count = read(descriptor, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            ok = count == 0;
            break;
        }
        if (static_cast<std::size_t>(count) > limit - bytes.size())
            ok = false;
        else
            bytes.append(buffer, static_cast<std::size_t>(count));
    }
    close(descriptor);
    if (ok)
        body = std::move(bytes);
    return ok;
}

bool copy_file(const std::string &from, const std::string &to)
{
    const int source = open(from.c_str(), O_RDONLY | O_NOFOLLOW);
    struct stat info
    {
    };
    if (source < 0 || fstat(source, &info) != 0 || !S_ISREG(info.st_mode))
    {
        if (source >= 0)
            close(source);
        return false;
    }
    const std::string temporary = to + ".store-new";
    unlink(temporary.c_str());
    const int target = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    bool ok = target >= 0;
    std::vector<char> buffer(256 * 1024);
    while (ok)
    {
        const auto count = read(source, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
        {
            ok = count == 0;
            break;
        }
        for (ssize_t done = 0; ok && done < count;)
        {
            const auto wrote =
                write(target, buffer.data() + done, static_cast<std::size_t>(count - done));
            if (wrote < 0 && errno == EINTR)
                continue;
            ok = wrote > 0;
            done += wrote > 0 ? wrote : 0;
        }
    }
    close(source);
    if (target >= 0)
    {
        ok = fsync(target) == 0 && ok;
        ok = close(target) == 0 && ok;
    }
    ok = ok && rename(temporary.c_str(), to.c_str()) == 0;
    if (!ok)
        unlink(temporary.c_str());
    return ok;
}

bool list_files(const std::string &folder, std::size_t limit, std::vector<std::string> &names)
{
    std::unique_ptr<DIR, decltype(&closedir)> directory(opendir(folder.c_str()), closedir);
    if (!directory)
        return false;
    for (;;)
    {
        errno = 0;
        const auto *entry = readdir(directory.get());
        if (!entry)
            return errno == 0;
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || kind(folder + "/" + name) != Kind::file)
            continue;
        if (names.size() >= limit)
            return false;
        names.push_back(name);
    }
}

bool remove_tree(const std::string &path)
{
    return remove_level(path, 0);
}
} // namespace store::install
