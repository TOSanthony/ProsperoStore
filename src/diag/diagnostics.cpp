// ProsperoStore - Durable logs and crash recovery lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag/diagnostics.hpp"
#include "core/save_file.hpp"
#include "../../examples/crash-report/crash_report.hpp"
#include <cstdio>
#include <unistd.h>

extern "C" int sceSystemServiceLoadExec(const char *, const char **);
namespace store::diag
{
namespace
{
void lifecycle(bool restart)
{
    sceSystemServiceLoadExec(restart ? "/app0/eboot.bin" : "exit", nullptr);
}
void rotate(const std::string &path)
{
    for (int i = 4; i > 0; --i)
        std::rename((path + "." + std::to_string(i)).c_str(),
                    (path + "." + std::to_string(i + 1)).c_str());
    std::rename(path.c_str(), (path + ".1").c_str());
}
} // namespace
bool start(const std::string &root)
{
    if (!hui::save::ensure_directory(root) || !hui::save::ensure_directory(root + "/logs"))
        return false;
    const auto directory = root + "/logs";
    const auto log = directory + "/app.log";
    rotate(log);
    rotate(directory + "/crash-latest.txt");
    // Reopen the runtime's FILE objects as in the qualified Eden lifecycle.
    std::fflush(nullptr);
    const bool redirected =
        std::freopen(log.c_str(), "a", stdout) && std::freopen(log.c_str(), "a", stderr);
    if (redirected)
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    return redirected && crash_report::install(directory.c_str(), "01.000.000", lifecycle);
}
void stop()
{
    crash_report::stop();
}
bool recovered()
{
    return crash_report::recovered();
}
} // namespace store::diag
