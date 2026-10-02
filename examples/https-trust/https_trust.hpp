// ps5-native-app-boilerplate - Explicit system trust for elevated HTTPS callers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>
namespace https_trust
{
// Caller reads the console's immutable CA bundle. Does not change TLS options.
// Returns the platform result; -1 means malformed or oversized PEM input.
int load_pem_roots(int http_context, std::string_view pem);
} // namespace https_trust
