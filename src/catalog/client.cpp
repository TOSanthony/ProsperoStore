// ProsperoStore - Verify before parsing and publish only a complete catalog generation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog/client.hpp"
#include "core/save_file.hpp"
#include <fcntl.h>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace store::catalog
{
namespace
{
constexpr const char *kApi = "https://homebrew.page/api/v1/";
bool flush_directory(const std::string &path)
{
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool ok = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && ok;
}
} // namespace

bool Client::file(const Manifest &manifest, const std::string &name, std::size_t limit,
                  std::string &body, net::Control *control, std::string &error)
{
    const auto found = manifest.files.find(name);
    if (found == manifest.files.end())
    {
        error = "The app is no longer listed";
        return false;
    }
    const std::string path = cache_ + "/" + found->second;
    if (hui::save::read_file(path, &body, limit) && manifest.verifies(name, body))
        return true;
    if (!control)
    {
        error = "The offline catalog is incomplete";
        return false;
    }
    const auto response = net::fetch(kApi + name, net::Purpose::catalog, limit, body, *control);
    if (!response.ok())
    {
        error = response.error;
        return false;
    }
    if (!manifest.verifies(name, body))
    {
        error = "The catalog file could not be verified";
        return false;
    }
    error = hui::save::write_atomic(path, body);
    return error.empty();
}

bool Client::parse(const std::string &bundle, std::uint64_t highest, Snapshot &out,
                   net::Control *control, std::string &error)
{
    if (bundle.size() <= 64)
    {
        error = "No verified offline catalog";
        return false;
    }
    Snapshot next;
    if (!verify_manifest(std::string_view(bundle).substr(64),
                         std::string_view(bundle).substr(0, 64), highest, public_keys(),
                         next.manifest, error))
        return false;
    std::string index, versions;
    if (!file(next.manifest, "index.json", kIndexLimit, index, control, error) ||
        !file(next.manifest, "versions.json", kVersionsLimit, versions, control, error) ||
        !parse_index(index, next.entries, error) || !parse_versions(versions, next.versions, error))
        return false;
    next.verified = true;
    next.online = control != nullptr;
    out = std::move(next);
    return true;
}

bool Client::cached(Snapshot &out, std::string &error)
{
    std::string bundle;
    if (!hui::save::read_file(cache_ + "/current", &bundle, kVersionsLimit + 64))
    {
        error = "No verified offline catalog";
        return false;
    }
    return parse(bundle, out.manifest.sequence, out, nullptr, error);
}

bool Client::refresh(Snapshot &out, net::Control &control, std::string &error)
{
    if (!hui::save::ensure_directory(cache_))
    {
        error = "The catalog cache is unavailable";
        return false;
    }
    // Recover the persisted high-water mark even if one cached data file is damaged.
    std::uint64_t highest = out.manifest.sequence;
    std::string current;
    const std::string trust_path = cache_ + "/current";
    struct stat trust_stat
    {
    };
    const int trust_exists = ::lstat(trust_path.c_str(), &trust_stat);
    if (trust_exists != 0 && errno != ENOENT)
    {
        error = "The saved catalog trust record is inaccessible";
        return false;
    }
    if (trust_exists == 0)
    {
        Manifest previous;
        std::string ignored;
        if (!S_ISREG(trust_stat.st_mode) ||
            !hui::save::read_file(trust_path, &current, kVersionsLimit + 64) ||
            current.size() <= 64 ||
            !verify_manifest(std::string_view(current).substr(64),
                             std::string_view(current).substr(0, 64), highest, public_keys(),
                             previous, ignored))
        {
            error = "The saved catalog trust record is damaged";
            return false;
        }
        highest = previous.sequence;
    }
    std::string body, signature;
    auto response = net::fetch(std::string(kApi) + "manifest.json", net::Purpose::catalog,
                               kVersionsLimit, body, control);
    if (!response.ok())
    {
        error = response.error;
        return false;
    }
    response = net::fetch(std::string(kApi) + "manifest.sig", net::Purpose::catalog, 64, signature,
                          control);
    if (!response.ok())
    {
        error = response.error;
        return false;
    }
    Snapshot next;
    const std::string bundle = signature + body;
    if (!parse(bundle, highest, next, &control, error))
        return false;
    // Data files are durable before the atomic pointer to the generation changes.
    if (!flush_directory(cache_))
    {
        error = "The catalog cache could not be committed";
        return false;
    }
    error = hui::save::write_atomic(cache_ + "/current", bundle);
    if (!error.empty() || !flush_directory(cache_))
    {
        error = "The catalog trust record could not be committed";
        return false;
    }
    out = std::move(next);
    return true;
}

bool Client::detail(const Snapshot &snapshot, const std::string &id, Entry &out,
                    net::Control &control, std::string &error)
{
    if (!snapshot.verified || !title_id(id))
    {
        error = "The catalog is not verified";
        return false;
    }
    std::string body;
    return file(snapshot.manifest, "apps/" + id + ".json", kDetailLimit, body, &control, error) &&
           parse_detail(body, id, out, error);
}
} // namespace store::catalog
