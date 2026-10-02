// ProsperoStore - Host HTTPS transport with the same redirect and size policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/http.hpp"
#include <curl/curl.h>
#include <memory>

namespace store::net
{
void Control::cancel()
{
    cancelled.store(true);
}

Response request_once(const std::string &url, std::uint64_t limit, const Sink &sink,
                      Control &control, const std::string &etag)
{
    Response out;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
    {
        out.error = "The network could not start";
        return out;
    }
    struct State
    {
        CURL *handle;
        Response &response;
        Control &control;
        const Sink &sink;
        std::uint64_t limit;
        std::string headers;
    } state{handle.get(), out, control, sink, limit, {}};
    const auto write =
        +[](char *data, std::size_t size, std::size_t count, void *opaque) -> std::size_t
    {
        auto &s = *static_cast<State *>(opaque);
        const auto length = size * count;
        long status = 0;
        curl_easy_getinfo(s.handle, CURLINFO_RESPONSE_CODE, &status);
        if (status != 200)
            return length;
        if (s.control.cancelled.load())
            return 0;
        if (length > s.limit - s.response.bytes)
        {
            s.response.error = "The response exceeds its size limit";
            return 0;
        }
        if (!s.sink({data, length}))
            return 0;
        s.response.bytes += length;
        return length;
    };
    const auto headers =
        +[](char *data, std::size_t size, std::size_t count, void *opaque) -> std::size_t
    {
        auto &s = *static_cast<State *>(opaque);
        const auto length = size * count;
        if (length > 64 * 1024 - s.headers.size())
            return 0;
        s.headers.append(data, length);
        return length;
    };
    const auto progress = +[](void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
    { return static_cast<State *>(opaque)->control.cancelled.load() ? 1 : 0; };
    std::string conditional = "If-None-Match: " + etag;
    curl_slist *list = etag.empty() ? nullptr : curl_slist_append(nullptr, conditional.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> owned_headers(list,
                                                                              curl_slist_free_all);
    if (!etag.empty() && !list)
    {
        out.error = "The request could not be created";
        return out;
    }
    auto *curl = handle.get();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ProsperoStore/01.000.000");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 5L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    out.status = static_cast<int>(status);
    if (control.cancelled.load())
        out.error = "Cancelled";
    else if (result != CURLE_OK && out.error.empty())
        out.error = "The network request failed";
    else if (!read_headers(state.headers, out))
        out.error = "The response headers are invalid";
    return out;
}
} // namespace store::net
