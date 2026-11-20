// SPDX-License-Identifier: MIT
#include "../../src/sessionarm.h"

#include <cstdio>
#include <fcntl.h>
#include <sys/socket.h>
#include <utility>
#include <cstdlib>
#include <string>

using rasklad::sessionarm::SessionArm;
using rasklad::sessionarm::safeCommitText;
using rasklad::sessionarm::ReceiptSink;
using rasklad::sessionarm::codePoints;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void pipePair(int fds[2]) {
    if (pipe(fds) != 0) std::abort();
}

int main() {
    {   // armed while writer open and silent; closing latches off forever
        int p[2]; pipePair(p);
        SessionArm arm(p[0]);
        CHECK(arm.armed());
        CHECK(arm.armed());
        close(p[1]);
        CHECK(!arm.armed());
        int q[2]; pipePair(q);  // a fresh pipe does not re-arm the latched gate
        CHECK(!arm.armed());
        close(q[0]); close(q[1]); close(p[0]);
    }
    {   // any byte written latches off
        int p[2]; pipePair(p);
        SessionArm arm(p[0]);
        CHECK(arm.armed());
        CHECK(write(p[1], "x", 1) == 1);
        CHECK(!arm.armed());
        close(p[1]);
        CHECK(!arm.armed());
        close(p[0]);
    }
    {   // already closed at start: never armed
        int p[2]; pipePair(p); close(p[1]);
        SessionArm arm(p[0]);
        CHECK(!arm.armed());
        close(p[0]);
    }
    {   // wrong descriptor kinds refused
        CHECK(!SessionArm(-1).armed());
        CHECK(!SessionArm(0).armed());
        CHECK(!SessionArm(2).armed());
        int p[2]; pipePair(p);
        CHECK(!SessionArm(p[1]).armed());  // write end
        char path[] = "/tmp/rasklad-armXXXXXX";
        int f = mkstemp(path);
        CHECK(f >= 0 && !SessionArm(f).armed());  // regular file
        unlink(path); close(f); close(p[0]); close(p[1]);
    }
    {   // environment parsing
        int p[2]; pipePair(p);
        std::string fd = std::to_string(p[0]);
        setenv("RASKLAD_SESSION_ARM_FD", fd.c_str(), 1);
        CHECK(SessionArm::fromEnvironment().armed());
        for (const char *bad : {"", "3x", "-3", "99999", " 3", "0x3"}) {
            setenv("RASKLAD_SESSION_ARM_FD", bad, 1);
            CHECK(!SessionArm::fromEnvironment().armed());
        }
        unsetenv("RASKLAD_SESSION_ARM_FD");
        CHECK(!SessionArm::fromEnvironment().armed());
        close(p[0]); close(p[1]);
    }
    {   // commit text allowlist: one ASCII/Cyrillic word, optionally one trailing space
        for (const char *good : {"привет ", "привет", "hello ", "hello", "Hello", "ёЁ", "ПРИВЕТ "}) CHECK(safeCommitText(good));
        CHECK(safeCommitText(std::string(256, 'a')));
        CHECK(!safeCommitText(std::string(257, 'a')));
        for (const char *bad : {"", " ", "a  ", "a b", " a", "a\n", "a\r", "a\t", "\x1b[0m", "a\x7f", "a1", "a-", "a.",
                                "\xc2\x85", "\xc2\x9f", "\xc2\xa0", "\xc2\xad", "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80\x8b",
                                "\xe2\x80\x8f", "\xe2\x80\xae", "\xe2\x81\xa6", "\xef\xbb\xbf", "\xef\xbf\xbf", "\xd1\xa0",
                                "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xd0", "a\xd0", "\xd0\x41",
                                "\x80", "\xff", "\xf5\x80\x80\x80", "\xf0\x9f\x98\x80"}) {
            CHECK(!safeCommitText(bad));
        }
        CHECK(!safeCommitText(std::string_view("a\0b", 3)));
    }
    {   // O_RDWR pipe end and socketpair refused; fd exactly 3 accepted when it is a read-only FIFO
        int p[2]; pipePair(p);
        int rw = open(("/proc/self/fd/" + std::to_string(p[0])).c_str(), O_RDWR);
        CHECK(rw >= 0 && !SessionArm(rw).armed());
        int sp[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0 && !SessionArm(sp[0]).armed());
        int devnull = open("/dev/null", O_RDONLY);
        CHECK(!SessionArm(devnull).armed());
        close(rw); close(sp[0]); close(sp[1]); close(devnull);
        close(p[0]); close(p[1]);
        int q[2]; pipePair(q);
        const int saved = q[0] == 3 ? -1 : fcntl(3, F_DUPFD_CLOEXEC, 10);
        if (q[0] != 3) { CHECK(dup2(q[0], 3) == 3); close(q[0]); }
        CHECK(SessionArm(3).armed());
        close(3); close(q[1]);
        if (saved >= 0) { dup2(saved, 3); close(saved); }
    }
    {   // byte written before construction; read end gets CLOEXEC
        int p[2]; pipePair(p);
        CHECK(write(p[1], "x", 1) == 1);
        CHECK(!SessionArm(p[0]).armed());
        close(p[0]); close(p[1]);
        pipePair(p);
        SessionArm arm(p[0]);
        CHECK((fcntl(p[0], F_GETFD) & FD_CLOEXEC) != 0);
        close(p[0]); close(p[1]);
    }
    {   // fd reuse: closed and reopened as another silent pipe stays latched off
        int p[2]; pipePair(p);
        const int fd = p[0];
        SessionArm arm(fd);
        CHECK(arm.armed());
        close(fd);
        int q[2]; pipePair(q);
        if (q[0] != fd) { dup2(q[0], fd); close(q[0]); }
        CHECK(!arm.armed());
        close(fd); close(q[1]); close(p[1]);
    }
    {   // move keeps state; moved-from is off
        int p[2]; pipePair(p);
        SessionArm a(p[0]);
        SessionArm b(std::move(a));
        CHECK(!a.armed());
        CHECK(b.armed());
        close(p[1]);
        CHECK(!b.armed());
        close(p[0]);
    }
    {   // env edge cases
        for (const char *bad : {"0003", "+3", "9999"}) {
            setenv("RASKLAD_SESSION_ARM_FD", bad, 1);
            CHECK(!SessionArm::fromEnvironment().armed());
        }
        unsetenv("RASKLAD_SESSION_ARM_FD");
    }
    {   // receipt sink: SEQPACKET only, fixed 4-byte records, no SIGPIPE when the reader is gone
        int sp[2];
        CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sp) == 0);
        ReceiptSink sink(sp[0]);
        CHECK(sink.valid());
        CHECK(sink.send(true, codePoints("привет ")));
        CHECK(sink.send(false, codePoints("hello ")));
        char buf[8] = {};
        CHECK(read(sp[1], buf, sizeof buf) == 4 && std::string(buf, 4) == "c07\n");
        CHECK(read(sp[1], buf, sizeof buf) == 4 && std::string(buf, 4) == "o06\n");
        CHECK(!sink.send(true, 0));
        CHECK(!sink.send(true, 100));
        close(sp[1]);
        CHECK(!sink.send(true, 7));  // peer closed: EPIPE without SIGPIPE, reported as failure
        close(sp[0]);
        int st[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, st) == 0 && !ReceiptSink(st[0]).valid());
        close(st[0]); close(st[1]);
        int p[2]; pipePair(p);
        CHECK(!ReceiptSink(p[1]).valid());
        close(p[0]); close(p[1]);
        CHECK(!ReceiptSink(-1).valid());
        unsetenv("RASKLAD_SESSION_RECEIPT_FD");
        CHECK(!ReceiptSink::fromEnvironment().valid());
    }
    {   // a local latch turns the gate off permanently
        int p[2]; pipePair(p);
        SessionArm arm(p[0]);
        CHECK(arm.armed());
        arm.latch();
        CHECK(!arm.armed());
        close(p[0]); close(p[1]);
    }
    if (failures) return 1;
    std::puts("sessionarm_tests OK");
    return 0;
}
