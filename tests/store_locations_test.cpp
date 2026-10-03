// ProsperoStore - Scan overrides must never expose staging as an installed app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/locations.hpp"
#include <algorithm>
#include <cassert>

int main()
{
    using namespace store::system;
    ScanPolicy policy;
    std::string error;
    assert(scan_policy("", "", policy, error));
    assert(policy.roots.size() == 34 && policy.depth == 1);
    assert(work_path_unscanned(policy, "/mnt/ext1/prosperostore/staging/PPSA99001"));
    assert(!work_path_unscanned(policy, "/mnt/ext1/PPSA99001"));
    assert(scan_policy("SCANPATH = /mnt/ext1/homebrew/ ; note\nscanpath=/data/homebrew\n"
                       "recursive_scan=YES\nscan_depth=1\n",
                       "# manual titles\n/data/custom/PPSA99002\n", policy, error));
    assert(policy.roots.size() == 4 && policy.depth == 2 && policy.manual.size() == 1);
    assert(std::find(policy.roots.begin(), policy.roots.end(), "/data/custom") ==
           policy.roots.end());
    assert(!work_path_unscanned(policy, "/data/custom/PPSA99002/nested"));
    assert(work_path_unscanned(policy, "/data/prosperostore/staging/PPSA99002"));
    assert(scan_policy("scanpath=/data/prosperostore\nscan_depth=2", "", policy, error));
    assert(!work_path_unscanned(policy, "/data/prosperostore/staging/PPSA99002"));
    assert(!scan_policy("scanpath=/data/../system", "", policy, error));
    assert(!scan_policy("scanpath=/data//homebrew", "", policy, error));
    assert(!scan_policy(std::string(300000, 'x'), "", policy, error));
    assert(drive_root("/mnt/ext1/homebrew") == "/mnt/ext1");
    assert(drive_root("/mnt/ext10/homebrew").empty());
    assert(drive_root("/system/app").empty());
    assert(scan_policy("scan_depth=0x2", "", policy, error) && policy.depth == 2);
    assert(scan_policy("scan_depth=02", "", policy, error) && policy.depth == 2);
    assert(scan_policy("recursive_scan=ro", "", policy, error) && policy.depth == 2);
}
