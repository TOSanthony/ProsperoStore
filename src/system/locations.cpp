// ProsperoStore - Bounded parsing of the installed scanner's configuration.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/locations.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>

namespace store::system
{
namespace
{
std::string_view trim(std::string_view value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    return begin == std::string_view::npos
               ? std::string_view{}
               : value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}
std::string lower(std::string_view value)
{
    std::string result(value);
    for (auto &c : result)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 'a' - 'A');
    return result;
}
bool within(std::string_view root, std::string_view path)
{
    return root == "/" || path == root ||
           (path.starts_with(root) && path.size() > root.size() && path[root.size()] == '/');
}
bool add(std::vector<std::string> &paths, std::string_view path)
{
    while (path.size() > 1 && path.ends_with('/'))
        path.remove_suffix(1);
    if (!clean_absolute_path(path))
        return false;
    if (std::find(paths.begin(), paths.end(), path) == paths.end())
    {
        if (paths.size() == 256)
            return false;
        paths.emplace_back(path);
    }
    return true;
}
} // namespace

bool clean_absolute_path(std::string_view path)
{
    if (path.empty() || path.front() != '/' || path.size() >= 1024 ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    if (path == "/")
        return true;
    path.remove_prefix(1);
    while (!path.empty())
    {
        auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == ".." || part.find('\\') != part.npos)
            return false;
        if (slash == path.npos)
            return true;
        path.remove_prefix(slash + 1);
    }
    return false;
}

bool scan_policy(std::string_view config, std::string_view manual, ScanPolicy &out,
                 std::string &error)
{
    if (config.size() > 256 * 1024 || manual.size() > 256 * 1024 ||
        config.find('\0') != config.npos || manual.find('\0') != manual.npos)
    {
        error = "ShadowMount configuration is too large or contains invalid data";
        return false;
    }
    ScanPolicy candidate;
    bool recursive = false;
    while (!config.empty())
    {
        const auto newline = config.find('\n');
        auto line = trim(config.substr(0, newline));
        config = newline == config.npos ? std::string_view{} : config.substr(newline + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[')
            continue;
        if (line.size() >= 511)
        {
            error = "ShadowMount configuration contains an oversized line";
            return false;
        }
        const auto equals = line.find('=');
        if (equals == line.npos)
            continue;
        const auto key = lower(trim(line.substr(0, equals)));
        auto value = line.substr(equals + 1);
        value = trim(value.substr(0, value.find_first_of("#;")));
        if (value.empty())
            continue;
        if (key == "scanpath" && !add(candidate.roots, value))
        {
            error = "A ShadowMount scan path cannot be interpreted safely";
            return false;
        }
        if (key == "scan_depth")
        {
            const std::string number(value);
            char *end = nullptr;
            errno = 0;
            const auto depth = std::strtoul(number.c_str(), &end, 0);
            if (!errno && end != number.c_str() && *end == '\0' && depth >= 1 && depth <= 2)
                candidate.depth = static_cast<unsigned>(depth);
        }
        if (key == "recursive_scan")
        {
            const auto boolean = lower(value);
            recursive |= boolean == "1" || boolean == "true" || boolean == "yes" ||
                         boolean == "on" || boolean == "ro";
        }
    }
    if (recursive)
        candidate.depth = 2;
    if (candidate.roots.empty())
    {
        candidate.roots = {"/data/homebrew", "/data/etaHEN/games"};
        std::vector<std::string> drives{"/mnt/ext0", "/mnt/ext1"};
        for (int i = 0; i < 8; ++i)
            drives.push_back("/mnt/usb" + std::to_string(i));
        for (const auto &drive : drives)
        {
            candidate.roots.push_back(drive + "/homebrew");
            candidate.roots.push_back(drive + "/etaHEN/games");
            candidate.roots.push_back(drive);
        }
    }
    if (!add(candidate.roots, "/mnt/shadowmnt/pfsc") || !add(candidate.roots, "/mnt/shadowmnt"))
    {
        error = "Too many ShadowMount scan paths";
        return false;
    }
    while (!manual.empty())
    {
        const auto newline = manual.find('\n');
        const auto line = trim(manual.substr(0, newline));
        manual = newline == manual.npos ? std::string_view{} : manual.substr(newline + 1);
        if (!line.empty() && line.front() != '#' && !add(candidate.manual, line))
        {
            error = "A ShadowMount manual path cannot be interpreted safely";
            return false;
        }
    }
    out = std::move(candidate);
    error.clear();
    return true;
}

std::string drive_root(std::string_view path)
{
    if (!clean_absolute_path(path))
        return {};
    if (within("/data", path))
        return "/data";
    if (within("/mnt/ext0", path))
        return "/mnt/ext0";
    if (within("/mnt/ext1", path))
        return "/mnt/ext1";
    for (int i = 0; i < 8; ++i)
    {
        const auto root = "/mnt/usb" + std::to_string(i);
        if (within(root, path))
            return root;
    }
    return {};
}

std::string work_path_conflict(const ScanPolicy &policy, std::string_view app_path)
{
    if (!clean_absolute_path(app_path))
        return "the path isn't a plain one";
    for (const auto &root : policy.roots)
    {
        if (within(app_path, root))
            return "scan path " + root + " is inside it";
        if (within(root, app_path))
        {
            const auto relative = app_path.substr(root == "/" ? 0 : root.size());
            const auto depth =
                static_cast<unsigned>(std::count(relative.begin(), relative.end(), '/'));
            const unsigned limit = policy.depth + (within("/mnt/shadowmnt/pfsc", root) ? 1u : 0u);
            if (depth <= limit)
                return "scan path " + root + ", scan depth " + std::to_string(policy.depth);
        }
    }
    for (const auto &path : policy.manual)
        if (within(path, app_path) || within(app_path, path))
            return "manual.lst entry " + path;
    return {};
}

bool work_path_unscanned(const ScanPolicy &policy, std::string_view app_path)
{
    return work_path_conflict(policy, app_path).empty();
}
} // namespace store::system
