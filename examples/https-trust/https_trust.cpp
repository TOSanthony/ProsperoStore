// ps5-native-app-boilerplate - Bounded root-CA import through the platform API.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "https_trust.hpp"
#include <array>
#include <cstddef>

namespace
{
// Public API data layout: pointer followed by a 64-bit byte length.
struct Certificate
{
    const void *data;
    std::size_t size;
};
extern "C" int sceHttpsLoadCert(int context, int count, const Certificate *const *roots,
                                const Certificate *client, const Certificate *key);
} // namespace
namespace https_trust
{
int load_pem_roots(int http_context, std::string_view pem)
{
    constexpr std::string_view begin = "-----BEGIN CERTIFICATE-----";
    constexpr std::string_view end = "-----END CERTIFICATE-----";
    if (http_context < 0 || pem.empty() || pem.size() > (512u << 10) ||
        pem.find('\0') != std::string_view::npos)
        return -1;
    std::array<Certificate, 256> certificates{};
    std::array<const Certificate *, 256> pointers{};
    std::size_t count = 0;
    while (true)
    {
        const auto first = pem.find(begin);
        if (first == std::string_view::npos)
            break;
        pem.remove_prefix(first);
        const auto last = pem.find(end);
        if (last == std::string_view::npos || last + end.size() > 16384 ||
            count == certificates.size())
            return -1;
        const auto length = last + end.size();
        if (pem.substr(begin.size(), last - begin.size()).find(begin) != std::string_view::npos)
            return -1;
        certificates[count] = {pem.data(), length};
        pointers[count] = &certificates[count];
        ++count;
        pem.remove_prefix(length);
    }
    if (count == 0)
        return -1;
    return sceHttpsLoadCert(http_context, static_cast<int>(count), pointers.data(), nullptr,
                            nullptr);
}
} // namespace https_trust
