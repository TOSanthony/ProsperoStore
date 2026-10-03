// ProsperoStore - Hostile archives, refusals, and a power cut at every transaction step.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/archive.hpp"
#include "install/files.hpp"
#include "install/transaction.hpp"
#include "system/inventory.hpp"
#include "third_party/miniz/miniz.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace store;
namespace
{
const std::string kId = "PPSA99500";
using Files = std::vector<std::pair<std::string, std::string>>;

std::string metadata(const std::string &id, const std::string &version)
{
    return "{\"titleId\":\"" + id + "\",\"contentVersion\":\"" + version +
           "\",\"localizedParameters\":{\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":"
           "\"Example App\"}}}";
}
Files app(const std::string &version, const std::string &id = kId)
{
    // Half incompressible, half not: both the stored and the deflated paths run.
    std::string program(200000, 'e');
    std::uint32_t state = 12345;
    for (std::size_t i = 0; i < program.size() / 2; ++i)
        program[i] = static_cast<char>((state = state * 1664525u + 1013904223u) >> 24);
    return {{id + "/", ""},
            {id + "/eboot.bin", program + version},
            {id + "/sce_sys/", ""},
            {id + "/sce_sys/param.json", metadata(id, version)},
            {id + "/assets/deep/readme.txt", "hello " + version}};
}
std::string zip(const Files &files)
{
    mz_zip_archive archive{};
    assert(mz_zip_writer_init_heap(&archive, 0, 0));
    for (const auto &[name, data] : files)
        assert(mz_zip_writer_add_mem(&archive, name.c_str(), data.data(), data.size(), 6));
    void *bytes = nullptr;
    std::size_t size = 0;
    assert(mz_zip_writer_finalize_heap_archive(&archive, &bytes, &size));
    std::string result(static_cast<const char *>(bytes), size);
    mz_free(bytes);
    mz_zip_writer_end(&archive);
    return result;
}
std::uint32_t u32(const std::string &bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}
std::uint16_t u16(const std::string &bytes, std::size_t offset)
{
    std::uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 2);
    return value;
}
// Offset of the index-th header in the archive's directory.
std::size_t central(const std::string &bytes, unsigned index)
{
    std::size_t offset = u32(bytes, bytes.size() - 22 + 16);
    for (unsigned i = 0; i < index; ++i)
        offset += 46 + u16(bytes, offset + 28) + u16(bytes, offset + 30) + u16(bytes, offset + 32);
    assert(bytes.compare(offset, 4, "PK\x01\x02") == 0);
    return offset;
}
void put(const fs::path &path, const std::string &bytes)
{
    std::ofstream(path, std::ios::binary) << bytes;
}
std::string get(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}
// Every path below root with its content: equal exactly when two trees are.
std::string tree(const fs::path &root)
{
    std::map<std::string, std::string> entries;
    if (fs::exists(root))
        for (const auto &entry : fs::recursive_directory_iterator(root))
            entries[entry.path().lexically_relative(root).string()] =
                entry.is_regular_file() ? get(entry.path()) : "<dir>";
    std::string result;
    for (const auto &[name, data] : entries)
        result += name + "\n" + data + "\n";
    return result;
}
std::string expected_tree(const Files &files)
{
    std::map<std::string, std::string> entries;
    for (auto [name, data] : files)
    {
        const bool directory = name.ends_with('/');
        if (directory)
            name.pop_back();
        if (name.size() <= kId.size())
            continue;
        name = name.substr(kId.size() + 1);
        for (auto slash = name.find('/'); slash != name.npos; slash = name.find('/', slash + 1))
            entries[name.substr(0, slash)] = "<dir>";
        entries[name] = directory ? "<dir>" : data;
    }
    std::string result;
    for (const auto &[name, data] : entries)
        result += name + "\n" + data + "\n";
    return result;
}

void check_archives(const fs::path &root)
{
    const auto file = (root / "a.zip").string();
    const auto refused = [&](const std::string &bytes)
    {
        put(file, bytes);
        install::ArchiveInfo info;
        std::string error;
        const bool ok = install::inspect_archive(file, kId, info, error);
        assert(ok || !error.empty());
        return !ok;
    };
    const auto good = app("01.000.001");
    put(file, zip(good));
    install::ArchiveInfo info;
    std::string error;
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    assert(info.unpacked == good[1].second.size() + good[3].second.size() + good[4].second.size());
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> written{0};
    const auto out = (root / "out").string();
    assert(install::extract_archive(file, kId, out, cancelled, written, error));
    assert(written == info.unpacked && tree(out) == expected_tree(good));
    // The destination must be new: nothing is ever unpacked over existing files.
    assert(!install::extract_archive(file, kId, out, cancelled, written, error));
    assert(install::remove_tree(out) && !fs::exists(out));

    assert(refused("not a zip"));
    assert(refused(zip(good).substr(0, zip(good).size() / 2)));
    // What lies outside the app's folder is ignored, whatever it is called.
    auto files = good;
    files.push_back({"README.md", "beside the folder"});
    files.push_back({"other/../escape.txt", "never unpacked"});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    assert(install::extract_archive(file, kId, out, cancelled, written, error));
    assert(tree(out) == expected_tree(good) && install::remove_tree(out));
    // The app at the top of the archive, and inside a release folder.
    for (const std::string &wrap :
         {std::string(), "Release-1.0/" + kId + "/", std::string("Release-1.0/")})
    {
        files.clear();
        for (auto [name, data] : good)
            if (name.size() > kId.size() + 1)
                files.push_back({wrap + name.substr(kId.size() + 1), data});
        files.push_back({wrap.empty() ? "notes.txt" : "notes.txt", "loose"});
        put(file, zip(files));
        written = 0;
        assert(install::inspect_archive(file, kId, info, error));
        assert(install::extract_archive(file, kId, out, cancelled, written, error));
        if (wrap.empty()) // Everything at the top belongs to the app, the note too.
            assert(get(fs::path(out) / "notes.txt") == "loose" && info.files == 4);
        else
            assert(tree(out) == expected_tree(good) && info.files == 3);
        assert(install::remove_tree(out));
    }
    // Of two apps side by side the one named after the title is taken; two
    // that are both strangers can't be told apart; a nested copy is not the app.
    files = good;
    for (const auto &[name, data] : app("01.000.001", "PPSA99501"))
        files.push_back({name, data});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    files = app("01.000.001", "PPSA99501");
    for (const auto &[name, data] : app("01.000.001", "PPSA99502"))
        files.push_back({name, data});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/data/sample/sce_sys/param.json", "{}"});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 4);
    files = good;
    files.erase(files.begin() + 1);
    assert(refused(zip(files))); // No eboot.bin.
    files = good;
    files.push_back({kId + "/../escape.txt", "parent path"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/a//b.txt", "empty component"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/./b.txt", "dot component"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/eboot.bin", "duplicate"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/" + std::string(install::kArchivePath, 'n'), "long name"});
    assert(refused(zip(files)));
    files = good;
    files.erase(files.begin() + 3);
    assert(refused(zip(files))); // No sce_sys/param.json.

    // Directory entries patched the way a hostile archive would be written.
    auto bytes = zip(good);
    auto header = central(bytes, 1);
    bytes[header + 46] = '/'; // An absolute path.
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 46 + 4] = '\\';
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 46 + 4] = '\x01';
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 5] = 3; // Written on Unix, as a symbolic link.
    const std::uint32_t link = 0120777u << 16;
    std::memcpy(bytes.data() + header + 38, &link, 4);
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 8] |= 1; // Encrypted.
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 10] = 12; // A compression method the reader doesn't support.
    assert(refused(bytes));

    // A directory that understates a file's size passes inspection by design:
    // the size is enforced while unpacking, and nothing more is ever written.
    bytes = zip(good);
    header = central(bytes, 1);
    const std::uint32_t lie = 1000;
    std::memcpy(bytes.data() + header + 24, &lie, 4);
    put(file, bytes);
    written = 0;
    if (install::inspect_archive(file, kId, info, error))
    {
        assert(!install::extract_archive(file, kId, out, cancelled, written, error));
        assert(!fs::exists(out + "/eboot.bin") || fs::file_size(out + "/eboot.bin") <= lie);
        assert(install::remove_tree(out));
    }
    // A cancelled unpack stops and reports it.
    put(file, zip(good));
    cancelled = true;
    assert(!install::extract_archive(file, kId, out, cancelled, written, error));
    assert(error == "Cancelled" && install::remove_tree(out));

    // remove_tree never follows a link out of the tree it was given.
    fs::create_directories(root / "keep");
    put(root / "keep/file.txt", "kept");
    fs::create_directories(root / "junk/sub");
    fs::create_directory_symlink(root / "keep", root / "junk/sub/link");
    assert(install::remove_tree((root / "junk").string()));
    assert(!fs::exists(root / "junk") && get(root / "keep/file.txt") == "kept");
    fs::remove(file);
}

struct Fixture
{
    fs::path root, location, state, work;
    install::Environment environment;
    std::string artifact;
    unsigned requests = 0, failures = 0;
    int running = 0;
    explicit Fixture(const fs::path &base, const std::string &name)
        : root(base / name), location(root / "apps"), state(root / "state"), work(root / "work")
    {
        fs::remove_all(root);
        fs::create_directories(location);
        fs::create_directories(state);
        environment.root = state.string();
        environment.self = "PPSA99000";
        environment.work = work.string();
        environment.registered = (root / "user").string();
        environment.policy.roots = {location.string()};
        environment.now = [] { return std::string("2026-10-02T00:00:00Z"); };
        environment.wait = [](unsigned, const std::atomic<bool> &) {};
        environment.running = [this](const std::string &) { return running; };
        environment.fetch = [this](const std::string &, std::uint64_t limit, const net::Sink &sink,
                                   net::Control &control)
        {
            ++requests;
            net::Response response;
            if (failures)
            {
                --failures;
                sink(std::string_view(artifact).substr(0, artifact.size() / 2));
                response.error = "The network request failed (curl 56)";
                return response;
            }
            for (std::size_t offset = 0; offset < artifact.size(); offset += 65536)
            {
                const auto chunk = std::string_view(artifact).substr(offset, 65536);
                if (control.cancelled.load() || offset + chunk.size() > limit || !sink(chunk))
                {
                    response.error = control.cancelled.load() ? "Cancelled" : "Refused";
                    return response;
                }
            }
            response.status = 200;
            return response;
        };
    }
    install::Request request(const std::string &version)
    {
        artifact = zip(app(version));
        install::Request result;
        result.entry.id = kId;
        result.entry.status = "available";
        result.entry.format = "zip";
        result.entry.version = "v" + version;
        result.entry.content_version = version;
        result.entry.artifact =
            "https://github.com/example/app/releases/download/v1/" + kId + ".zip";
        result.entry.digest = catalog::sha256(artifact);
        result.entry.size = artifact.size();
        result.location = location.string();
        return result;
    }
    install::Result apply(const install::Request &request)
    {
        net::Control control;
        install::Progress progress;
        return install::apply(environment, request, control, progress);
    }
    install::Result uninstall()
    {
        install::Progress progress;
        return install::uninstall(environment, kId, location.string(), progress);
    }
    fs::path target() const
    {
        return location / kId;
    }
    // "", or the version the inventory reports as managed by the store.
    std::string managed() const
    {
        std::atomic<bool> cancelled{false};
        const auto inventory =
            system::scan_installed(environment.policy, (state / "receipts").string(), cancelled);
        for (const auto &app : inventory.apps)
            if (app.id == kId && app.managed)
                return app.version;
        return {};
    }
    // No journal and nothing left in the work folders.
    bool settled() const
    {
        for (const auto *folder : {"staging", "backup", "trash"})
            if (fs::exists(work / folder) && !fs::is_empty(work / folder))
                return false;
        return !fs::exists(state / "journal.json") && !fs::exists(state / "journal.json.tmp");
    }
};

void check_transactions(const fs::path &base)
{
    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    Fixture f(base, "basic");
    auto request = f.request("01.000.001");
    auto result = f.apply(request);
    assert(result.ok && result.operation == "install" && result.version == "01.000.001");
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());
    assert(get(f.state / "receipts" / (kId + ".json")).find("\"releaseTag\":\"v01.000.001\"") !=
           std::string::npos);

    // The same version again is not an update, and nothing is downloaded for it.
    f.requests = 0;
    result = f.apply(request);
    assert(!result.ok && f.requests == 0 && tree(f.target()) == v1 && f.settled());

    // Updates need a definite "not running".
    request = f.request("01.000.002");
    f.running = 1;
    result = f.apply(request);
    assert(!result.ok && result.error == "Close the app first" && f.requests == 0);
    f.running = -1;
    assert(!f.apply(request).ok && f.requests == 0);
    f.environment.running = nullptr;
    assert(!f.apply(request).ok && f.requests == 0 && tree(f.target()) == v1);
    f.environment.running = [&f](const std::string &) { return f.running; };
    // Running is asked again after the download, right before the swap.
    unsigned asked = 0;
    f.environment.running = [&asked](const std::string &) { return asked++ == 0 ? 0 : 1; };
    result = f.apply(request);
    assert(!result.ok && result.error == "Close the app first" && asked == 2);
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());
    f.environment.running = [&f](const std::string &) { return f.running; };
    f.running = 0;
    // The console's own copies of the app's sce_sys, as made at registration.
    fs::create_directories(f.root / "user/appmeta" / kId);
    fs::create_directories(f.root / "user/app" / kId / "sce_sys");
    put(f.root / "user/appmeta" / kId / "param.json", "old");
    put(f.root / "user/app" / kId / "sce_sys/param.json", "old");
    put(f.root / "user/app" / kId / "icon0.png", "old icon");
    result = f.apply(request);
    assert(result.ok && result.operation == "update" && result.version == "01.000.002");
    assert(get(f.root / "user/appmeta" / kId / "param.json") == metadata(kId, "01.000.002"));
    assert(get(f.root / "user/app" / kId / "sce_sys/param.json") == metadata(kId, "01.000.002"));
    assert(get(f.root / "user/app" / kId / "icon0.png") == "old icon"); // The app ships none.
    assert(tree(f.target()) == v2 && f.managed() == "01.000.002" && f.settled());

    // A listing that claims a newer version than the file holds is not an update.
    request = f.request("01.000.002");
    request.entry.content_version = "01.000.003";
    result = f.apply(request);
    assert(!result.ok && tree(f.target()) == v2 && f.managed() == "01.000.002" && f.settled());

    // Uninstall: refused while running or unknown, then complete.
    f.running = 1;
    assert(!f.uninstall().ok && tree(f.target()) == v2);
    f.running = 0;
    result = f.uninstall();
    assert(result.ok && !fs::exists(f.target()) && f.managed().empty() && f.settled());
    assert(!fs::exists(f.state / "receipts" / (kId + ".json")));
    assert(!f.uninstall().ok);

    // Refusals before anything is requested.
    request = f.request("01.000.001");
    const auto refused = [&](install::Request changed)
    {
        f.requests = 0;
        const auto outcome = f.apply(changed);
        return !outcome.ok && !outcome.error.empty() && f.requests == 0 &&
               !fs::exists(f.target()) && f.settled();
    };
    auto changed = request;
    changed.entry.format = "ffpfsc";
    assert(refused(changed));
    changed = request;
    changed.entry.status = "coming_soon";
    assert(refused(changed));
    changed = request;
    changed.entry.artifact = "https://example.com/" + kId + ".zip";
    assert(refused(changed));
    changed = request;
    changed.entry.artifact = "https://release-assets.githubusercontent.com/x.zip";
    assert(refused(changed)); // Only reachable by redirect from github.com.
    changed = request;
    changed.entry.digest = "abc";
    assert(refused(changed));
    changed = request;
    changed.entry.size = catalog::kArtifactLimit + 1;
    assert(refused(changed));
    changed = request;
    changed.minimum_version = "01.000.002";
    assert(refused(changed));
    changed = request;
    changed.entry.id = "PPSA99000";
    assert(refused(changed));
    changed = request;
    changed.location = f.root.string();
    assert(refused(changed)); // Not a scanned location.
    changed = request;
    changed.location = f.location.string() + "/../apps";
    assert(refused(changed));
    f.environment.policy.roots.push_back(f.work.string());
    f.environment.policy.depth = 2;
    assert(refused(request)); // The work folder would be scanned.
    f.environment.policy.roots.pop_back();
    f.environment.policy.depth = 1;
    f.environment.space = [](const std::string &, std::uint64_t &bytes)
    {
        bytes = 1000;
        return true;
    };
    result = f.apply(request);
    assert(!result.ok && result.error.starts_with("Not enough space") && f.requests == 0);
    assert(!fs::exists(f.target()) && f.settled());
    // Room for the download, not for what it unpacks to.
    f.environment.space = [&](const std::string &, std::uint64_t &bytes)
    {
        bytes = f.artifact.size() + (17u << 20);
        return true;
    };
    result = f.apply(request);
    assert(!result.ok && result.error.starts_with("Not enough space") && f.requests == 1);
    assert(!fs::exists(f.target()) && f.settled());
    f.environment.space = nullptr;

    // A file that isn't the listed one is never unpacked, and isn't retried.
    changed = request;
    changed.entry.digest = std::string(64, 'a');
    f.requests = 0;
    result = f.apply(changed);
    assert(!result.ok && result.error == "The file doesn't match the listing" && f.requests == 1);
    assert(!fs::exists(f.target()) && f.settled());
    changed = request;
    changed.entry.size += 1;
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    changed = request;
    changed.entry.size -= 1; // The transport's limit is the listed size.
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    // A listed, correctly hashed archive that holds another title's app.
    changed = request;
    f.artifact = zip(app("01.000.001", "PPSA99501"));
    changed.entry.digest = catalog::sha256(f.artifact);
    changed.entry.size = f.artifact.size();
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    // The folder is right but its param.json names another title.
    auto files = app("01.000.001");
    files[3].second = metadata("PPSA99501", "01.000.001");
    f.artifact = zip(files);
    changed.entry.digest = catalog::sha256(f.artifact);
    changed.entry.size = f.artifact.size();
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());

    // A broken connection is retried from the start, and the result still verifies.
    request = f.request("01.000.001");
    f.requests = 0;
    f.failures = 2;
    result = f.apply(request);
    assert(result.ok && f.requests == 3 && tree(f.target()) == v1 && f.settled());
    assert(f.uninstall().ok);
    f.requests = 0;
    f.failures = 3;
    result = f.apply(request);
    assert(!result.ok && f.requests == 3 && !fs::exists(f.target()) && f.settled());
    f.failures = 0;

    // Cancel during the download.
    {
        net::Control control;
        install::Progress progress;
        const auto fetch = f.environment.fetch;
        f.environment.fetch = [&](const std::string &url, std::uint64_t limit,
                                  const net::Sink &sink, net::Control &inner)
        {
            return fetch(
                url, limit,
                [&](std::string_view chunk)
                {
                    inner.cancelled = true;
                    return sink(chunk);
                },
                inner);
        };
        result = install::apply(f.environment, request, control, progress);
        assert(!result.ok && result.error == "Cancelled" && !fs::exists(f.target()));
        assert(f.settled());
        f.environment.fetch = fetch;
    }

    // An app the store didn't install is never touched.
    fs::create_directories(f.target() / "sce_sys");
    put(f.target() / "sce_sys/param.json", metadata(kId, "01.000.000"));
    const auto foreign = tree(f.target());
    f.requests = 0;
    result = f.apply(f.request("01.000.002"));
    assert(!result.ok && f.requests == 0 && tree(f.target()) == foreign && f.settled());
    assert(!f.uninstall().ok && tree(f.target()) == foreign);
    // Nor is one whose receipt names another version or location.
    put(f.state / "receipts" / (kId + ".json"),
        catalog::format_receipt(
            {kId, f.location.string(), "01.000.009", "v", std::string(64, 'a'), "now"}));
    assert(!f.apply(f.request("01.000.010")).ok && !f.uninstall().ok);
    assert(tree(f.target()) == foreign);
    fs::remove_all(f.target());
    put(f.target(), "a file where the app folder would go");
    assert(!f.apply(f.request("01.000.001")).ok && fs::is_regular_file(f.target()));
    fs::remove(f.target());

    // A journal that can't be read turns installing off instead of guessing.
    put(f.state / "journal.json", "{\"schema\":1,\"operation\":\"install\"}");
    assert(!install::recover(f.environment).ok);
    assert(!f.apply(f.request("01.000.001")).ok && !fs::exists(f.target()));
    put(f.state / "journal.json",
        catalog::format_journal({"install", "swap", kId, f.location.string(), "", "", ""}));
    assert(!install::recover(f.environment).ok);
    fs::remove(f.state / "journal.json");
    assert(install::recover(f.environment).ok);
}

// Stops dead at every step of every operation, as a power cut would, and
// checks the folder is whole before and after recovery.
void check_interruptions(const fs::path &base)
{
    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    for (const std::string operation : {"install", "update", "uninstall"})
    {
        std::vector<std::string> steps;
        for (std::size_t stop = 0; stop == 0 || stop <= steps.size(); ++stop)
        {
            Fixture f(base, "cut-" + operation + "-" + std::to_string(stop));
            if (operation != "install")
                assert(f.apply(f.request("01.000.001")).ok);
            const auto before = operation == "install" ? std::string{} : v1;
            const auto after = operation == "install"  ? v1
                               : operation == "update" ? v2
                                                       : std::string{};
            const auto run = [&]
            {
                return operation == "uninstall"
                           ? f.uninstall()
                           : f.apply(
                                 f.request(operation == "install" ? "01.000.001" : "01.000.002"));
            };
            std::vector<std::string> seen;
            f.environment.interrupt = [&](const char *step)
            {
                seen.emplace_back(step);
                return stop != 0 && seen.size() == stop;
            };
            const auto result = run();
            if (stop == 0)
            {
                assert(result.ok && tree(f.target()) == after && f.settled());
                steps = seen;
                assert(steps.size() >= 4);
                continue;
            }
            assert(result.interrupted && !result.ok);
            auto now = tree(f.target());
            // Whole or absent at the instant of the cut, never a mix.
            assert(now == before || now == after || now.empty());
            f.environment.interrupt = nullptr;
            const auto recovered = install::recover(f.environment);
            assert(recovered.ok && recovered.operation == operation);
            now = tree(f.target());
            assert((now == before || now == after) && f.settled());
            // What is there is managed with its true version; what isn't has no receipt.
            const auto version = f.managed();
            assert(version == (now == v1 ? "01.000.001" : now == v2 ? "01.000.002" : ""));
            assert(now.empty() == !fs::exists(f.state / "receipts" / (kId + ".json")));
            assert(install::recover(f.environment).ok); // Recovery is safe to repeat.
            // The interrupted operation can simply be asked for again.
            if (now != after)
                assert(run().ok);
            assert(tree(f.target()) == after && f.settled());
            fs::remove_all(f.root);
        }
    }
}

void check_records()
{
    catalog::Receipt receipt{"PPSA99500",   "/data/home\"brew",   "01.000.001",
                             "v1 \"beta\"", std::string(64, 'b'), "2026-10-02T00:00:00Z"},
        parsed;
    std::string error;
    assert(catalog::parse_receipt(catalog::format_receipt(receipt), parsed, error));
    assert(parsed.location == receipt.location && parsed.release_tag == receipt.release_tag);
    catalog::Journal journal{"update",     "swap", "PPSA99500",         "/data/homebrew",
                             "01.000.002", "v2",   std::string(64, 'c')},
        read;
    assert(catalog::parse_journal(catalog::format_journal(journal), read, error));
    assert(read.state == "swap" && read.content_version == "01.000.002");
    journal.state = "remove"; // Not a state of an update.
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    journal = {"install", "activate", "PPSA99500", "/data/homebrew", "", "", ""};
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    journal = {"uninstall", "remove", "../escape", "/data/homebrew", "", "", ""};
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    assert(!catalog::parse_journal(std::string(20000, ' '), read, error));
}
} // namespace

int main()
{
    char temporary[] = "/tmp/prospero-install-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path root(temporary);
    check_records();
    check_archives(root);
    check_transactions(root);
    check_interruptions(root);
    fs::remove_all(root);
    std::puts("Install, update, uninstall and recovery checks passed");
}
