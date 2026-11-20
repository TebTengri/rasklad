// SPDX-License-Identifier: MIT
// Supervised Slack session LAB gate (RASKLAD_SLACK_SESSION_LAB builds only).
//
// SessionArm: the supervisor keeps the write end of a pipe; the addon receives
// the read end as RASKLAD_SESSION_ARM_FD. The session is armed only while the
// writer is open and silent. Closing it (revoke, supervisor exit/crash) or any
// byte written latches the gate off for the life of the process: fail closed.
//
// safeCommitText: last check before commitString(); valid UTF-8 without C0/C1
// controls, DEL or line/paragraph separators.
#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rasklad::sessionarm {

class SessionArm {
public:
    // Reads and validates RASKLAD_SESSION_ARM_FD once. Invalid → never armed.
    static SessionArm fromEnvironment() {
        const char *raw = std::getenv("RASKLAD_SESSION_ARM_FD");
        if (!raw || !*raw || std::strlen(raw) > 4) return SessionArm(-1);
        int fd = 0;
        for (const char *p = raw; *p; ++p) {
            if (*p < '0' || *p > '9') return SessionArm(-1);
            fd = fd * 10 + (*p - '0');
        }
        return SessionArm(fd);
    }

    explicit SessionArm(int fd) {
        struct stat st {};
        if (fd < 3 || fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode)) return;
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || (flags & O_ACCMODE) != O_RDONLY) return;
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return;
        fd_ = fd;
        latched_ = false;
        latched_ = !poll0(); // already closed or written: never armed
    }

    SessionArm(const SessionArm &) = delete;
    SessionArm &operator=(const SessionArm &) = delete;
    SessionArm(SessionArm &&other) noexcept : fd_(other.fd_), latched_(other.latched_) {
        other.fd_ = -1;
        other.latched_ = true;
    }

    bool armed() {
        if (latched_) return false;
        if (!poll0()) latched_ = true;
        return !latched_;
    }

private:
    // True only when the pipe reports no event at all (writer open, no data).
    bool poll0() const {
        if (fd_ < 0) return false;
        pollfd p{fd_, POLLIN, 0};
        int rc;
        do {
            rc = ::poll(&p, 1, 0);
        } while (rc < 0 && errno == EINTR);
        return rc == 0;
    }

    int fd_ = -1;
    bool latched_ = true;
};

inline bool safeCommitText(std::string_view text) {
    if (text.empty() || text.size() > 256) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        unsigned cp = 0;
        std::size_t n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1f; n = 2; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0f; n = 3; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 0x07; n = 4; }
        else return false;
        if (i + n > text.size()) return false;
        for (std::size_t k = 1; k < n; ++k) {
            const auto cc = static_cast<unsigned char>(text[i + k]);
            if ((cc & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3f);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10ffff))) return false; // overlong / range
        if (cp >= 0xd800 && cp <= 0xdfff) return false;                                          // surrogates
        if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp <= 0x9f)) return false;                // C0, DEL, C1
        if (cp == 0x2028 || cp == 0x2029) return false;                                         // line/para sep
        i += n;
    }
    return true;
}

} // namespace rasklad::sessionarm
