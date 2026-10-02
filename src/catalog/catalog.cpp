// ProsperoStore - Strict bounded JSON, signed manifests and download policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog/catalog.hpp"
#include "third_party/monocypher/monocypher-ed25519.h"
#include "third_party/picosha2/picosha2.h"
#include "third_party/yyjson/yyjson.h"

#include <algorithm>
#include <memory>
#include <set>

namespace store::catalog
{
namespace
{
bool unique_members(yyjson_val *value, unsigned depth = 0)
{
    if (depth > 32)
        return false;
    if (yyjson_is_obj(value))
    {
        std::set<std::string_view> keys;
        std::size_t index = 0, count = 0;
        yyjson_val *key = nullptr, *child = nullptr;
        yyjson_obj_foreach(value, index, count, key, child)
        {
            if (!keys.insert({yyjson_get_str(key), yyjson_get_len(key)}).second ||
                !unique_members(child, depth + 1))
                return false;
        }
    }
    else if (yyjson_is_arr(value))
    {
        std::size_t index = 0, count = 0;
        yyjson_val *child = nullptr;
        yyjson_arr_foreach(value, index, count,
                           child) if (!unique_members(child, depth + 1)) return false;
    }
    return true;
}

struct Json
{
    std::vector<char> pool;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc{nullptr, yyjson_doc_free};
    yyjson_val *read(std::string_view body, std::size_t limit)
    {
        if (body.empty() || body.size() > limit)
            return nullptr;
        pool.resize(yyjson_read_max_memory_usage(body.size(), 0));
        yyjson_alc allocator{};
        if (!yyjson_alc_pool_init(&allocator, pool.data(), pool.size()))
            return nullptr;
        doc.reset(
            yyjson_read_opts(const_cast<char *>(body.data()), body.size(), 0, &allocator, nullptr));
        if (!doc)
            return nullptr;
        auto *root = yyjson_doc_get_root(doc.get());
        auto *schema = yyjson_obj_get(root, "schema");
        return yyjson_is_obj(root) && yyjson_is_uint(schema) && yyjson_get_uint(schema) == 3 &&
                       unique_members(root)
                   ? root
                   : nullptr;
    }
};

bool field(yyjson_val *object, const char *key, std::string &out, std::size_t limit,
           bool required = false)
{
    auto *value = yyjson_obj_get(object, key);
    if (!value || yyjson_is_null(value))
    {
        out.clear();
        return !required;
    }
    if (!yyjson_is_str(value) || yyjson_get_len(value) > limit)
        return false;
    out.assign(yyjson_get_str(value), yyjson_get_len(value));
    return out.find('\0') == std::string::npos && (!required || !out.empty());
}

bool read_entry(yyjson_val *value, Entry &entry, bool detail)
{
    if (!yyjson_is_obj(value) || !field(value, "titleid", entry.id, 9, true) ||
        !title_id(entry.id) || !field(value, "name", entry.name, 256, true) ||
        !field(value, "author", entry.author, 256) || !field(value, "kind", entry.kind, 32, true) ||
        !field(value, "status", entry.status, 32, true) ||
        !field(value, "version", entry.version, 128) ||
        !field(value, "content_version", entry.content_version, 10) ||
        !field(value, "format", entry.format, 16) || !field(value, "icon_small", entry.icon, 512) ||
        !field(value, "icon_hash", entry.icon_hash, 64) ||
        !field(value, "released", entry.released, 40) ||
        !field(value, "updated", entry.updated, 40))
        return false;
    if (entry.status != "available" && entry.status != "coming_soon")
        return false;
    if (!entry.icon.empty() && !api_url(entry.icon))
        return false;
    if (!entry.content_version.empty() && !version(entry.content_version))
        return false;
    auto *size = yyjson_obj_get(value, "size");
    if (size && !yyjson_is_null(size))
    {
        if (!yyjson_is_uint(size))
            return false;
        entry.size = yyjson_get_uint(size);
    }
    if (!detail)
        return true;
    if (!field(value, "description", entry.description, 32768) ||
        !field(value, "license", entry.license, 256) ||
        !field(value, "source_repo", entry.source, 1024) ||
        !field(value, "page", entry.page, 512) ||
        !field(value, "artifact_url", entry.artifact, 4096, entry.status == "available") ||
        !field(value, "sha256", entry.digest, 64, entry.status == "available") ||
        !field(value, "release_notes", entry.release_notes, 32768))
        return false;
    std::array<std::uint8_t, 32> digest{};
    return entry.status != "available" || hex_bytes(entry.digest, digest);
}

std::string_view host(std::string_view url)
{
    if (!url.starts_with("https://") || url.size() > 4096 ||
        std::any_of(url.begin(), url.end(),
                    [](unsigned char c) { return c <= 32 || c >= 127 || c == '\\'; }))
        return {};
    const auto slash = url.find('/', 8);
    if (slash == std::string_view::npos)
        return {};
    return url.substr(8, slash - 8);
}
} // namespace

bool title_id(std::string_view value)
{
    return value.size() == 9 && value.starts_with("PPSA") &&
           std::all_of(value.begin() + 4, value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool version(std::string_view value)
{
    if (value.size() != 10 || value[2] != '.' || value[6] != '.')
        return false;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (i != 2 && i != 6 && (value[i] < '0' || value[i] > '9'))
            return false;
    return true;
}

bool update_available(std::string_view installed, std::string_view available)
{
    return version(installed) && version(available) && available > installed;
}

bool api_url(std::string_view url)
{
    return host(url) == "homebrew.page";
}
bool artifact_url(std::string_view url, bool redirected)
{
    const auto domain = host(url);
    return domain == "github.com" ||
           (redirected && domain == "release-assets.githubusercontent.com");
}

std::string sha256(std::string_view bytes)
{
    std::array<std::uint8_t, 32> digest{};
    picosha2::hash256(bytes.begin(), bytes.end(), digest.begin(), digest.end());
    // Avoid the library's iostream formatter: the PS5 runtime has no locale backend.
    constexpr char digits[] = "0123456789abcdef";
    std::string text(64, '0');
    for (std::size_t i = 0; i < digest.size(); ++i)
    {
        text[i * 2] = digits[digest[i] >> 4];
        text[i * 2 + 1] = digits[digest[i] & 15];
    }
    return text;
}

bool hex_bytes(std::string_view text, std::span<std::uint8_t> bytes)
{
    if (text.size() != bytes.size() * 2)
        return false;
    const auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const int a = digit(text[i * 2]), b = digit(text[i * 2 + 1]);
        if (a < 0 || b < 0)
            return false;
        bytes[i] = static_cast<std::uint8_t>(a * 16 + b);
    }
    return true;
}

std::array<PublicKey, 2> public_keys()
{
    std::array<PublicKey, 2> keys{};
    hex_bytes("87391bf1698ecef101bf5e29dc8585ee5947d571e19470de7411c5d3b137b5cf", keys[0]);
    hex_bytes("509bcfab7edfb4e5ed23639488517c6ef2657c13b6b7f2bf699c8989d9b0dd7b", keys[1]);
    return keys;
}

bool Manifest::verifies(std::string_view path, std::string_view body) const
{
    const auto entry = files.find(std::string(path));
    return entry != files.end() && entry->second == sha256(body);
}

bool verify_manifest(std::string_view body, std::string_view signature, std::uint64_t highest,
                     std::span<const PublicKey> keys, Manifest &out, std::string &error)
{
    error = "The catalog could not be verified";
    if (body.empty() || body.size() > kVersionsLimit || signature.size() != 64)
        return false;
    bool signed_by_us = false;
    for (const auto &key : keys)
        signed_by_us |= crypto_ed25519_check(
                            reinterpret_cast<const std::uint8_t *>(signature.data()), key.data(),
                            reinterpret_cast<const std::uint8_t *>(body.data()), body.size()) == 0;
    if (!signed_by_us)
        return false;
    Json json;
    auto *root = json.read(body, kVersionsLimit);
    auto *sequence = yyjson_obj_get(root, "sequence");
    auto *files = yyjson_obj_get(root, "files");
    if (!root || !yyjson_is_uint(sequence) || !yyjson_is_obj(files))
        return false;
    Manifest candidate;
    candidate.sequence = yyjson_get_uint(sequence);
    if (candidate.sequence < highest)
    {
        error = "An older catalog was refused";
        return false;
    }
    if (!field(root, "commit", candidate.commit, 64, true))
        return false;
    std::size_t index = 0, count = 0;
    yyjson_val *key = nullptr, *value = nullptr;
    yyjson_obj_foreach(files, index, count, key, value)
    {
        const std::string path(yyjson_get_str(key), yyjson_get_len(key));
        if (path != "index.json" && path != "versions.json" &&
            !(path.size() == 19 && path.starts_with("apps/") && path.ends_with(".json") &&
              title_id(std::string_view(path).substr(5, 9))))
            return false;
        if (!yyjson_is_str(value))
            return false;
        const std::string digest(yyjson_get_str(value), yyjson_get_len(value));
        std::array<std::uint8_t, 32> bytes{};
        if (!hex_bytes(digest, bytes))
            return false;
        candidate.files.emplace(path, digest);
    }
    if (!candidate.files.contains("index.json") || !candidate.files.contains("versions.json"))
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

bool parse_index(std::string_view body, std::vector<Entry> &out, std::string &error)
{
    error = "The catalog response is invalid";
    Json json;
    auto *root = json.read(body, kIndexLimit);
    auto *apps = yyjson_obj_get(root, "apps");
    if (!root || !yyjson_is_arr(apps) || yyjson_arr_size(apps) > 10000)
        return false;
    std::vector<Entry> entries;
    std::set<std::string> ids;
    std::size_t index = 0, count = 0;
    yyjson_val *value = nullptr;
    yyjson_arr_foreach(apps, index, count, value)
    {
        Entry entry;
        if (!read_entry(value, entry, false) || !ids.insert(entry.id).second)
            return false;
        entries.push_back(std::move(entry));
    }
    out = std::move(entries);
    error.clear();
    return true;
}

bool parse_detail(std::string_view body, std::string_view expected, Entry &out, std::string &error)
{
    error = "The app response is invalid";
    Json json;
    auto *root = json.read(body, kDetailLimit);
    Entry candidate;
    if (!root || !read_entry(root, candidate, true) || candidate.id != expected)
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

bool parse_versions(std::string_view body, std::map<std::string, std::string> &out,
                    std::string &error)
{
    error = "The versions response is invalid";
    Json json;
    auto *root = json.read(body, kVersionsLimit);
    auto *apps = yyjson_obj_get(root, "apps");
    if (!root || !yyjson_is_obj(apps))
        return false;
    std::map<std::string, std::string> versions;
    std::size_t index = 0, count = 0;
    yyjson_val *key = nullptr, *value = nullptr;
    yyjson_obj_foreach(apps, index, count, key, value)
    {
        const std::string id(yyjson_get_str(key), yyjson_get_len(key));
        std::string content;
        if (!title_id(id) || !yyjson_is_obj(value) ||
            !field(value, "content_version", content, 10) ||
            (!content.empty() && !version(content)))
            return false;
        versions.emplace(id, content);
    }
    out = std::move(versions);
    error.clear();
    return true;
}
} // namespace store::catalog
