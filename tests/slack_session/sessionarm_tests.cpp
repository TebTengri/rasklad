// SPDX-License-Identifier: MIT
#include "../../src/sessionarm.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using rasklad::sessionarm::SessionArm;
using rasklad::sessionarm::safeCommitText;

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
    {   // commit text
        CHECK(safeCommitText("привет "));
        CHECK(safeCommitText("hello "));
        CHECK(!safeCommitText(""));
        CHECK(!safeCommitText(std::string(257, 'a')));
        for (const char *bad : {"a\n", "a\r", "a\t", "\x1b[0m", "a\x7f", "\xc2\x85", "\xc2\x9f", "\xe2\x80\xa8", "\xe2\x80\xa9",
                                "\xc0\xaf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xd0", "\x80", "\xff"}) {
            CHECK(!safeCommitText(bad));
        }
        CHECK(!safeCommitText(std::string_view("a\0b", 3)));
    }
    if (failures) return 1;
    std::puts("sessionarm_tests OK");
    return 0;
}
