// ProsperoStore - Durable logs and crash recovery lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace store::diag
{
bool start(const std::string &root);
void stop();
bool recovered();
} // namespace store::diag
