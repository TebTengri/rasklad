// SPDX-License-Identifier: MIT
#include "manualgesture.h"
#include "layoutconverter.h"

#include <QString>
#include <cstdio>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

using rasklad::manual::DoubleShift;
using rasklad::manual::Input;

int main() {
    // Table equals the reviewed converter for every letter.
    for (char c = 'a'; c <= 'z'; ++c) {
        const std::string one(1, c);
        const QString expected = rasklad::convertLayout(QString::fromLatin1(one.c_str()), rasklad::LayoutDirection::UsToRu);
        CHECK(QString::fromStdString(rasklad::manual::usToRu(one)) == expected);
    }
    CHECK(rasklad::manual::usToRu("ghbdtn") == "привет");
    CHECK(rasklad::manual::usToRu("gh1").empty());
    CHECK(rasklad::manual::usToRu("A").empty());

    DoubleShift g;
    // Quick double tap.
    CHECK(!g.feed(Input::ShiftPress, 1000)); CHECK(!g.feed(Input::ShiftRelease, 1050));
    CHECK(!g.feed(Input::ShiftPress, 1200)); CHECK(g.feed(Input::ShiftRelease, 1250));
    // Fires once, then starts over: a third tap alone does nothing.
    CHECK(!g.feed(Input::ShiftPress, 1300)); CHECK(!g.feed(Input::ShiftRelease, 1350));
    g.reset();
    // Too slow.
    g.feed(Input::ShiftPress, 0); g.feed(Input::ShiftRelease, 50);
    g.feed(Input::ShiftPress, 451); CHECK(!g.feed(Input::ShiftRelease, 500));
    g.reset();
    // Exactly at the window edge counts.
    g.feed(Input::ShiftPress, 0); g.feed(Input::ShiftRelease, 50);
    g.feed(Input::ShiftPress, 450); CHECK(g.feed(Input::ShiftRelease, 500));
    // Another key in between breaks it (Shift+letter, any other key).
    g.feed(Input::ShiftPress, 0); g.feed(Input::Other, 10); CHECK(!g.feed(Input::ShiftRelease, 20));
    g.feed(Input::ShiftPress, 100); CHECK(!g.feed(Input::ShiftRelease, 120));
    g.reset();
    g.feed(Input::ShiftPress, 0); g.feed(Input::ShiftRelease, 10); g.feed(Input::Other, 20);
    g.feed(Input::ShiftPress, 30); CHECK(!g.feed(Input::ShiftRelease, 40));
    g.reset();
    // Both Shifts held together is a chord, not two taps.
    g.feed(Input::ShiftPress, 0); g.feed(Input::ShiftPress, 5);
    CHECK(!g.feed(Input::ShiftRelease, 10)); CHECK(!g.feed(Input::ShiftRelease, 15));
    // Orphan release never counts.
    g.reset();
    CHECK(!g.feed(Input::ShiftRelease, 0)); g.feed(Input::ShiftPress, 10);
    CHECK(!g.feed(Input::ShiftRelease, 20));
    // Clock going backwards: not a double tap.
    g.reset();
    g.feed(Input::ShiftPress, 1000); g.feed(Input::ShiftRelease, 1050);
    g.feed(Input::ShiftPress, 900); CHECK(!g.feed(Input::ShiftRelease, 950));

    if (failures) return 1;
    std::printf("manual gesture tests OK\n");
    return 0;
}
