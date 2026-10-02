// ProsperoStore - Read-only live catalog trust probe for host development.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/client.hpp"
#include <cstdio>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    store::catalog::Client client(argv[1]);
    store::catalog::Snapshot snapshot;
    store::net::Control control;
    std::string error;
    client.cached(snapshot, error);
    if (!client.refresh(snapshot, control, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("Verified catalog sequence=%llu apps=%zu versions=%zu\n",
                static_cast<unsigned long long>(snapshot.manifest.sequence),
                snapshot.entries.size(), snapshot.versions.size());
    for (const auto &entry : snapshot.entries)
    {
        store::catalog::Entry detail;
        if (!client.detail(snapshot, entry.id, detail, control, error))
        {
            std::fprintf(stderr, "%s: %s\n", entry.id.c_str(), error.c_str());
            return 1;
        }
        std::printf("%s %s %s\n", entry.id.c_str(), entry.status.c_str(), entry.name.c_str());
    }
    store::catalog::Snapshot offline;
    if (!client.cached(offline, error) || offline.entries.size() != snapshot.entries.size())
        return 1;
    std::puts("Offline signature and content-hash revalidation passed");
}
