// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "device_count.hpp"
#include "utf8.hpp"

#include <cctype>
#include <cstddef>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace spn {

namespace {

std::string lower(std::string s) {
    for (char &c : s) c = spn::utf8::lowerChar(c);
    return s;
}

}  // namespace

int countSpaceMiceInText(const std::string &text) {
    static const std::string kPrefix = "n: name=\"";
    int count = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = lower(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.compare(0, kPrefix.size(), kPrefix) != 0) continue;
        if (line.find("3dconnexion") == std::string::npos) continue;
        if (line.find("keyboard") != std::string::npos ||
            line.find("consumer control") != std::string::npos ||
            line.find("system control") != std::string::npos)
            continue;
        ++count;
    }
    return count;
}

int countSpaceMice() {
#ifndef __linux__
    return -1;  // /proc/bus/input exists on Linux only; other OSes enumerate HID devices themselves
#else
    int fd = ::open("/proc/bus/input/devices", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    std::string text;
    char buf[4096];
    while (text.size() < 256 * 1024) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        text.append(buf, (size_t)n);
    }
    ::close(fd);
    return countSpaceMiceInText(text);
#endif
}

}  // namespace spn
