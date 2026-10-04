// ProsperoStore - Where the running store's own files are.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/app_folder.hpp"
#include <sys/stat.h>

namespace store::system
{
const std::string &app_folder()
{
    static const std::string folder = []
    {
        for (const char *candidate :
             {"/app0", "/system_ex/app/PPSA99000", "/mnt/sandbox/PPSA99000_000/app0"})
        {
            struct stat info
            {
            };
            if (stat((std::string(candidate) + "/eboot.bin").c_str(), &info) == 0 &&
                S_ISREG(info.st_mode))
                return std::string(candidate);
        }
        return std::string("/app0");
    }();
    return folder;
}
} // namespace store::system
