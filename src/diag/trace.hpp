// ProsperoStore - The debug build's trace: what the store found at each step, for reports.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

namespace store::diag
{
// Adds one line ("step: result") to the trace. Each line also goes to the kernel
// log and is appended to the trace file, wherever one could be written: the store's
// folder in /data when it has access, else the first USB drive that takes it.
void trace(const char *format, ...) __attribute__((format(printf, 1, 2)));
// The lines so far, for the About room.
std::vector<std::string> trace_lines();
// Where the trace file is being written ("" when nowhere could be written).
std::string trace_file();

// The checks a report needs, before and after elevation: firmware, clock, the
// app's folder, /data access, the payload loader on port 9021.
void trace_console(const char *when);
// One HEAD request to jellyfinarr.synology.me through libcurl, with every step curl reports.
void curl_probe(const char *when);
} // namespace store::diag
