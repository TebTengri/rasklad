// SPDX-License-Identifier: MIT
// SYS-226: the synthesized cue equals SOUND-001 (70 ms, click-free, bounded level).
#include "layoutcue.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

int main(int argc, char **argv)
{
    const auto s = rasklad::cue::synthesize(1.0);
    CHECK(s.size() == 3360);
    CHECK(s.front() == 0 && s.back() == 0);
    int peak = 0;
    for (const auto v : s) peak = std::max(peak, std::abs(static_cast<int>(v)));
    CHECK(peak > 3000 && peak <= 7865); // at most 0.24 of full scale
    const auto quiet = rasklad::cue::synthesize(0.45);
    int qpeak = 0;
    for (const auto v : quiet) qpeak = std::max(qpeak, std::abs(static_cast<int>(v)));
    CHECK(qpeak < peak / 2);
    // Optional: compare with the SOUND-001 WAV (44-byte header, S16LE mono).
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        CHECK(b.size() == 44 + 2 * s.size());
        int maxDiff = 0;
        for (std::size_t i = 0; i < s.size() && b.size() >= 44 + 2 * s.size(); ++i) {
            const auto v = static_cast<std::int16_t>(static_cast<unsigned char>(b[44 + 2 * i]) |
                                                     (static_cast<unsigned char>(b[45 + 2 * i]) << 8));
            maxDiff = std::max(maxDiff, std::abs(v - s[i]));
        }
        std::printf("max sample difference vs reference: %d\n", maxDiff);
        CHECK(maxDiff <= 1);
    }
    std::printf(failures ? "layout cue tests FAILED\n" : "layout cue tests OK\n");
    return failures ? 1 : 0;
}
