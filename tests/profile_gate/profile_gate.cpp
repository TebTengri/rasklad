// SPDX-License-Identifier: MIT
// SYS-256: profile_gate <profile root> - exit 0 only if the addon's verifier accepts
// the profile with the production pins. Prints the error category, never contents.
#include "profileverify.h"

#include <QString>

#include <cstdio>

namespace profile = rasklad::profile;

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fputs("usage: profile_gate <profile root>\n", stderr);
        return 2;
    }
    profile::VerifiedProfile verified;
    const profile::ProfileError error =
        profile::verifyProfileFiles(QString::fromLocal8Bit(argv[1]), profile::productionPins(), verified);
    if (error != profile::ProfileError::None) {
        std::fprintf(stderr, "profile refused: %s\n", profile::profileErrorName(error));
        return 1;
    }
    std::puts("profile accepted");
    return 0;
}
