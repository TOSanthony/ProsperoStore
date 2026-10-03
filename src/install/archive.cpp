// ProsperoStore - ZIP artifacts: validated from the directory, then unpacked.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/archive.hpp"
#include "install/files.hpp"
#include "third_party/miniz/miniz.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace store::install
{
namespace
{
constexpr std::size_t kMemoryBudget = 64u << 20;
constexpr std::size_t kBlockHeader = 16;

struct Entry
{
    mz_uint index = 0;
    std::string relative; // Below the title's folder; empty for the folder itself.
    bool directory = false;
    std::uint64_t size = 0;
};

// Every allocation the ZIP reader makes is charged to one budget, so a hostile
// directory cannot make it take the app's memory.
struct Budget
{
    std::size_t used = 0;
};
void *budget_realloc(void *opaque, void *address, std::size_t items, std::size_t size)
{
    auto &budget = *static_cast<Budget *>(opaque);
    if (size != 0 && items > (kMemoryBudget - kBlockHeader) / size)
        return nullptr;
    const std::size_t wanted = items * size;
    auto *block = address ? static_cast<unsigned char *>(address) - kBlockHeader : nullptr;
    std::size_t previous = 0;
    if (block)
        std::memcpy(&previous, block, sizeof(previous));
    if (wanted > kMemoryBudget - (budget.used - previous))
        return nullptr;
    auto *next = static_cast<unsigned char *>(std::realloc(block, wanted + kBlockHeader));
    if (!next)
        return nullptr;
    std::memcpy(next, &wanted, sizeof(wanted));
    budget.used = budget.used - previous + wanted;
    return next + kBlockHeader;
}
void *budget_alloc(void *opaque, std::size_t items, std::size_t size)
{
    return budget_realloc(opaque, nullptr, items, size);
}
void budget_free(void *opaque, void *address)
{
    if (!address)
        return;
    auto *block = static_cast<unsigned char *>(address) - kBlockHeader;
    std::size_t previous = 0;
    std::memcpy(&previous, block, sizeof(previous));
    static_cast<Budget *>(opaque)->used -= previous;
    std::free(block);
}

class Reader
{
  public:
    Reader() = default;
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    ~Reader()
    {
        if (open_)
            mz_zip_reader_end(&zip_);
        if (descriptor_ >= 0)
            close(descriptor_);
    }
    bool open(const std::string &path)
    {
        descriptor_ = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
        struct stat info
        {
        };
        if (descriptor_ < 0 || fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_size <= 0)
            return false;
        mz_zip_zero_struct(&zip_);
        zip_.m_pRead = +[](void *opaque, mz_uint64 offset, void *buffer, std::size_t count)
        {
            const int descriptor = *static_cast<int *>(opaque);
            std::size_t done = 0;
            while (done < count)
            {
                const auto result = pread(descriptor, static_cast<char *>(buffer) + done,
                                          count - done, static_cast<off_t>(offset + done));
                if (result < 0 && errno == EINTR)
                    continue;
                if (result <= 0)
                    break;
                done += static_cast<std::size_t>(result);
            }
            return done;
        };
        zip_.m_pIO_opaque = &descriptor_;
        zip_.m_pAlloc = budget_alloc;
        zip_.m_pFree = budget_free;
        zip_.m_pRealloc = budget_realloc;
        zip_.m_pAlloc_opaque = &budget_;
        open_ = mz_zip_reader_init(&zip_, static_cast<mz_uint64>(info.st_size), 0) != 0;
        return open_;
    }
    mz_zip_archive *zip()
    {
        return &zip_;
    }

  private:
    mz_zip_archive zip_{};
    Budget budget_;
    int descriptor_ = -1;
    bool open_ = false;
};

bool clean_relative(std::string_view path)
{
    while (!path.empty())
    {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (slash == path.npos)
            break;
        path.remove_prefix(slash + 1);
    }
    return true;
}

bool list(Reader &reader, std::string_view title, std::vector<Entry> &entries, ArchiveInfo &info,
          std::string &error)
{
    auto *zip = reader.zip();
    const mz_uint count = mz_zip_reader_get_num_files(zip);
    if (count == 0 || count > kArchiveEntries)
    {
        error = "The archive is empty or has too many entries";
        return false;
    }
    const std::string prefix = std::string(title) + "/";
    std::set<std::string> seen;
    bool metadata = false;
    std::vector<char> buffer(kArchivePath + 2);
    for (mz_uint index = 0; index < count; ++index)
    {
        error = "The archive isn't a valid app";
        const mz_uint length = mz_zip_reader_get_filename(zip, index, nullptr, 0);
        if (length < 2 || length - 1 > kArchivePath)
            return false;
        mz_zip_reader_get_filename(zip, index, buffer.data(), static_cast<mz_uint>(buffer.size()));
        std::string name(buffer.data(), length - 1);
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(zip, index, &stat) || !stat.m_is_supported ||
            stat.m_is_encrypted)
            return false;
        for (const unsigned char c : name)
            if (c < 32 || c == 127 || c == '\\')
                return false;
        if (!name.starts_with(prefix))
        {
            error = "The archive must hold one folder named after the app";
            return false;
        }
        Entry entry;
        entry.index = index;
        entry.directory = name.ends_with('/');
        if (entry.directory != (stat.m_is_directory != 0))
            return false;
        if (entry.directory)
            name.pop_back();
        entry.relative = name.size() > prefix.size() ? name.substr(prefix.size()) : std::string{};
        if (!entry.directory && entry.relative.empty())
            return false;
        if (!clean_relative(entry.relative) || !seen.insert(entry.relative).second)
            return false;
        // Entries written on Unix carry a file type: only files and folders pass.
        if ((stat.m_version_made_by >> 8) == 3)
        {
            const auto type = (stat.m_external_attr >> 16) & 0170000u;
            if (type != 0 && type != (entry.directory ? 0040000u : 0100000u))
            {
                error = "The archive contains a link or a special file";
                return false;
            }
        }
        if (entry.directory)
        {
            if (stat.m_uncomp_size != 0)
                return false;
        }
        else
        {
            entry.size = stat.m_uncomp_size;
            if (entry.size > std::numeric_limits<std::uint64_t>::max() - info.unpacked)
                return false;
            info.unpacked += entry.size;
            ++info.files;
            metadata |= entry.relative == "sce_sys/param.json";
        }
        entries.push_back(std::move(entry));
    }
    if (!metadata)
    {
        error = "The archive has no sce_sys/param.json";
        return false;
    }
    error.clear();
    return true;
}

// Creates the folders of relative below base. Each must be a real directory.
bool make_parents(const std::string &base, std::string_view relative, bool last_is_directory,
                  std::set<std::string> &made)
{
    std::size_t end = 0;
    for (;;)
    {
        end = relative.find('/', end);
        if (end == relative.npos && !last_is_directory)
            return true;
        const std::string part(relative.substr(0, end));
        if (made.insert(part).second && !make_directory(base + "/" + part))
            return false;
        if (end == relative.npos)
            return true;
        ++end;
    }
}

struct Output
{
    int descriptor;
    std::uint64_t expected, written = 0;
    const std::atomic<bool> &cancelled;
    std::atomic<std::uint64_t> &total;
};
std::size_t write_output(void *opaque, mz_uint64 offset, const void *data, std::size_t count)
{
    auto &output = *static_cast<Output *>(opaque);
    if (output.cancelled.load() || offset != output.written ||
        count > output.expected - output.written)
        return 0;
    std::size_t done = 0;
    while (done < count)
    {
        const auto result =
            write(output.descriptor, static_cast<const char *>(data) + done, count - done);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return 0;
        done += static_cast<std::size_t>(result);
    }
    output.written += count;
    output.total.fetch_add(count);
    return count;
}
} // namespace

bool inspect_archive(const std::string &path, std::string_view title, ArchiveInfo &out,
                     std::string &error)
{
    Reader reader;
    if (!reader.open(path))
    {
        error = "The archive isn't a valid app";
        return false;
    }
    std::vector<Entry> entries;
    ArchiveInfo info;
    if (!list(reader, title, entries, info, error))
        return false;
    out = info;
    return true;
}

bool extract_archive(const std::string &path, std::string_view title,
                     const std::string &destination, const std::atomic<bool> &cancelled,
                     std::atomic<std::uint64_t> &written, std::string &error)
{
    Reader reader;
    std::vector<Entry> entries;
    ArchiveInfo info;
    if (!reader.open(path))
    {
        error = "The archive isn't a valid app";
        return false;
    }
    if (!list(reader, title, entries, info, error))
        return false;
    error = "The app could not be unpacked";
    if (mkdir(destination.c_str(), 0755) != 0)
        return false;
    std::set<std::string> made;
    for (const auto &entry : entries)
    {
        if (cancelled.load())
        {
            error = "Cancelled";
            return false;
        }
        if (entry.relative.empty())
            continue;
        if (!make_parents(destination, entry.relative, entry.directory, made))
            return false;
        if (entry.directory)
            continue;
        const std::string target = destination + "/" + entry.relative;
        Output output{open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644),
                      entry.size, 0, cancelled, written};
        if (output.descriptor < 0)
            return false;
        // The reader checks the stored CRC-32; the callback enforces the declared size.
        bool ok = mz_zip_reader_extract_to_callback(reader.zip(), entry.index, write_output,
                                                    &output, 0) != 0 &&
                  output.written == output.expected;
        ok = fsync(output.descriptor) == 0 && ok;
        ok = close(output.descriptor) == 0 && ok;
        if (!ok)
        {
            if (cancelled.load())
                error = "Cancelled";
            return false;
        }
    }
    for (const auto &directory : made)
        if (!sync_directory(destination + "/" + directory))
            return false;
    if (!sync_directory(destination))
        return false;
    error.clear();
    return true;
}
} // namespace store::install
