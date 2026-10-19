// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux hidraw backend: /dev/hidraw* + sysfs for names, HIDIOCGRDESC for the descriptor.
// Opens devices O_RDONLY and never writes.
#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "hidprobe_posix.hpp"

namespace hidprobe {
namespace {

class LinuxBackend : public HidBackend {
public:
    std::vector<DeviceInfo> list() override {
        std::vector<DeviceInfo> v;
        DIR *d = ::opendir("/dev");
        if (!d) return v;
        while (struct dirent *e = ::readdir(d)) {
            if (std::strncmp(e->d_name, "hidraw", 6) != 0) continue;
            DeviceInfo di;
            di.path = std::string("/dev/") + e->d_name;
            std::ifstream f(std::string("/sys/class/hidraw/") + e->d_name + "/device/uevent");
            std::stringstream ss;
            ss << f.rdbuf();
            if (!parseUevent(ss.str(), di)) continue;
            v.push_back(di);
        }
        ::closedir(d);
        std::sort(v.begin(), v.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
            return a.path.size() != b.path.size() ? a.path.size() < b.path.size() : a.path < b.path;
        });
        return v;
    }

    bool readDescriptor(const DeviceInfo &dev, std::vector<uint8_t> &out,
                        std::string &err) override {
        int fd = ::open(dev.path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            err = std::string("open ") + dev.path + ": " + std::strerror(errno);
            return false;
        }
        int size = 0;
        bool ok = false;
        if (::ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0 || size > HID_MAX_DESCRIPTOR_SIZE) {
            err = "HIDIOCGRDESCSIZE failed";
        } else {
            struct hidraw_report_descriptor rd;
            std::memset(&rd, 0, sizeof rd);
            rd.size = unsigned(size);
            if (::ioctl(fd, HIDIOCGRDESC, &rd) < 0) {
                err = "HIDIOCGRDESC failed";
            } else {
                out.assign(rd.value, rd.value + size);
                ok = true;
            }
        }
        ::close(fd);
        return ok;
    }

    std::unique_ptr<ReportStream> open(const DeviceInfo &dev, std::string &err,
                                       bool &permissionDenied) override {
        permissionDenied = false;
        int fd = ::open(dev.path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            permissionDenied = errno == EACCES || errno == EPERM;
            err = std::string("open ") + dev.path + ": " + std::strerror(errno);
            return nullptr;
        }
        return std::unique_ptr<ReportStream>(new FdReportStream(fd));
    }

    std::string permissionHelp(const DeviceInfo &dev) const override {
        char v[16];
        std::snprintf(v, sizeof v, "%04x", dev.vid);
        return "Permission denied. /dev/hidraw* is normally readable by root only.\n"
               "Quick fix (one run):  sudo dofscroll-hidprobe <same arguments>\n"
               "Permanent fix (read access for the logged-in user, no group membership needed):\n"
               "  echo 'KERNEL==\"hidraw*\", ATTRS{idVendor}==\"" +
               std::string(v) +
               "\", TAG+=\"uaccess\"' | sudo tee /etc/udev/rules.d/71-dofscroll-hidprobe.rules\n"
               "  sudo udevadm control --reload && sudo udevadm trigger\n"
               "then unplug and replug the device (see docs/devices.md). hidprobe never changes "
               "permissions itself.";
    }
};

}  // namespace

std::unique_ptr<HidBackend> makeBackend() { return std::unique_ptr<HidBackend>(new LinuxBackend); }

}  // namespace hidprobe
