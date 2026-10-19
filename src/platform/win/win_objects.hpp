// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Named kernel objects of the Windows daemon (declaration shared by win_platform.cpp and
// win_main.cpp). No Windows header: the handle is a void*.
#pragma once
#include <string>

namespace spn {

// Creates a named auto-reset event that only the current user and SYSTEM can open. Fails closed:
// returns nullptr (and sets `err`) if the access control list cannot be built, if the creation
// fails, or if an object of that name already exists (another process, possibly a squatter, owns
// it). The caller closes the handle with CloseHandle.
void *createPrivateEvent(const char *name, std::string *err);

}  // namespace spn
