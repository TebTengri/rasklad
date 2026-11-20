// SPDX-License-Identifier: MIT
// Supervised Slack session LAB gate (RASKLAD_SLACK_SESSION_LAB builds only).
//
// SessionArm: the supervisor keeps the write end of a pipe; the addon receives
// the read end as RASKLAD_SESSION_ARM_FD. The session is armed only while the
// writer is open and silent. Closing it (revoke, supervisor exit/crash) or any
// byte written latches the gate off for the life of the process: fail closed.
//
// Contract for the launcher/supervisor: the write end is held only by the
// supervisor (O_CLOEXEC, never inherited by Fcitx or its children); only the
// read end is passed to Fcitx. Revoke writes one byte AND closes, so a leaked
// write end still cannot keep the gate open; the launcher test checks that the
// write end is absent from /proc/<fcitx>/fd. Fcitx event handling is single-threaded; the
// latch is not designed for concurrent callers.
//
// safeCommitText: last check before commitString(); allowlist — one word of
// ASCII or Cyrillic letters, optionally followed by exactly one U+0020.
#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
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
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return;
        fd_ = fd;
        dev_ = st.st_dev;
        ino_ = st.st_ino;
        latched_ = false;
        latched_ = !poll0(); // already closed or written: never armed
    }

    SessionArm(const SessionArm &) = delete;
    SessionArm &operator=(const SessionArm &) = delete;
    SessionArm(SessionArm &&other) noexcept
        : fd_(other.fd_), dev_(other.dev_), ino_(other.ino_), latched_(other.latched_) {
        other.fd_ = -1;
        other.latched_ = true;
    }

    // Local fail-closed latch (e.g. a receipt could not be delivered).
    void latch() { latched_ = true; }

    bool armed() {
        if (latched_) return false;
        if (!sameFile() || !poll0()) latched_ = true;
        return !latched_;
    }

    // Descriptor for the event-loop watcher; -1 when never valid.
    int fd() const { return fd_; }

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

    // The descriptor must still be the pipe validated at start (no fd reuse).
    bool sameFile() const {
        struct stat st {};
        return fd_ >= 0 && fstat(fd_, &st) == 0 && S_ISFIFO(st.st_mode) && st.st_dev == dev_ &&
               st.st_ino == ino_;
    }

    int fd_ = -1;
    dev_t dev_ = 0;
    ino_t ino_ = 0;
    bool latched_ = true;
};

// ReceiptSink: after each commit the addon reports only its kind and length to
// the supervisor — never the text. RASKLAD_SESSION_RECEIPT_FD must be an
// AF_UNIX SOCK_SEQPACKET socket; one 4-byte record per commit: "c07\n"
// (rule correction, 7 code points) or "o06\n" (original). MSG_NOSIGNAL: a
// closed supervisor cannot kill Fcitx with SIGPIPE; any failure is reported
// to the caller, which latches the session off.
class ReceiptSink {
public:
    static ReceiptSink fromEnvironment() {
        const char *raw = std::getenv("RASKLAD_SESSION_RECEIPT_FD");
        if (!raw || !*raw || std::strlen(raw) > 4) return ReceiptSink(-1);
        int fd = 0;
        for (const char *p = raw; *p; ++p) {
            if (*p < '0' || *p > '9') return ReceiptSink(-1);
            fd = fd * 10 + (*p - '0');
        }
        return ReceiptSink(fd);
    }

    explicit ReceiptSink(int fd) {
        struct stat st {};
        int type = 0;
        socklen_t len = sizeof type;
        if (fd < 3 || fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode)) return;
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0 || type != SOCK_SEQPACKET) return;
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return;
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return;
        fd_ = fd;
        dev_ = st.st_dev;
        ino_ = st.st_ino;
    }

    ReceiptSink(const ReceiptSink &) = delete;
    ReceiptSink &operator=(const ReceiptSink &) = delete;
    ReceiptSink(ReceiptSink &&other) noexcept : fd_(other.fd_), dev_(other.dev_), ino_(other.ino_) { other.fd_ = -1; }

    bool valid() const {
        struct stat st {};
        return fd_ >= 0 && fstat(fd_, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_dev == dev_ && st.st_ino == ino_;
    }

    bool send(bool corrected, std::size_t codePoints) const {
        if (!valid() || codePoints == 0 || codePoints > 99) return false;
        const char record[4] = {corrected ? 'c' : 'o', static_cast<char>('0' + codePoints / 10),
                                static_cast<char>('0' + codePoints % 10), '\n'};
        ssize_t n;
        do {
            n = ::send(fd_, record, sizeof record, MSG_NOSIGNAL | MSG_DONTWAIT);
        } while (n < 0 && errno == EINTR);
        return n == static_cast<ssize_t>(sizeof record);
    }

private:
    int fd_ = -1;
    dev_t dev_ = 0;
    ino_t ino_ = 0;
};

inline std::size_t codePoints(std::string_view text) {
    std::size_t n = 0;
    for (unsigned char c : text) n += (c & 0xc0) != 0x80;
    return n;
}

inline bool safeCommitText(std::string_view text) {
    if (text.empty() || text.size() > 256) return false;
    std::size_t letters = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            ++letters;
            ++i;
            continue;
        }
        if (c == ' ') return letters > 0 && i + 1 == text.size(); // one trailing space only
        // Cyrillic letters U+0400..U+045F: lead 0xD0 (0x80..0xBF) or 0xD1 (0x80..0x9F).
        if (i + 1 < text.size()) {
            const auto n = static_cast<unsigned char>(text[i + 1]);
            if ((c == 0xd0 && n >= 0x80 && n <= 0xbf) || (c == 0xd1 && n >= 0x80 && n <= 0x9f)) {
                ++letters;
                i += 2;
                continue;
            }
        }
        return false;
    }
    return letters > 0;
}

} // namespace rasklad::sessionarm
