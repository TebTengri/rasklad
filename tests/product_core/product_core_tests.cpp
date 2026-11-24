// SPDX-License-Identifier: MIT
#include "applicationpolicy.h"
#include "configrules.h"
#include "wordrules.h"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

using namespace rasklad;
using Views = std::vector<std::string_view>;
using Kept = std::vector<std::size_t>;

static void configRules()
{
    CHECK(config::appIdentifier("org.telegram.desktop"));
    CHECK(config::appIdentifier("chrome-chatgpt.com__-Default"));
    CHECK(!config::appIdentifier(""));
    CHECK(!config::appIdentifier("a b"));
    CHECK(!config::appIdentifier("app/x"));
    CHECK(!config::appIdentifier("приложение"));
    CHECK(config::appIdentifier(std::string(128, 'a')));
    CHECK(!config::appIdentifier(std::string(129, 'a')));
    CHECK(!config::listableApp("kitty"));
    CHECK(config::listableApp("slack"));

    // Order kept; invalid, terminal and repeated entries dropped.
    CHECK((config::keptApplications(Views{"slack", "bad id", "kitty", "slack", "chromium"}) == Kept{0, 4}));
    CHECK(config::keptApplications(Views{}).empty());
    // The cap counts kept entries, not raw ones.
    std::vector<std::string> many;
    many.push_back("bad id");
    for (int i = 0; i < 70; ++i) many.push_back("app" + std::to_string(i));
    const Views manyViews(many.begin(), many.end());
    const auto keptApps = config::keptApplications(manyViews);
    CHECK(keptApps.size() == config::kMaxApps);
    CHECK(keptApps.front() == 1 && keptApps.back() == config::kMaxApps);

    // One entry per keys: case and layout do not make a new word.
    CHECK((config::keptExceptions(Views{"ok", "OK", "щл", "kek", "ok,", "", "лол"}) == Kept{0, 3, 6}));
    std::vector<std::string> words;
    words.push_back("ok");
    for (int i = 0; i < 300; ++i) {
        std::string w;
        for (int n = i + 1; n > 0; n /= 26) w += char('a' + n % 26);
        words.push_back(w);
    }
    words.insert(words.begin() + 2, "OK"); // a repeat early on does not use up the cap
    const Views wordViews(words.begin(), words.end());
    const auto keptWords = config::keptExceptions(wordViews);
    CHECK(keptWords.size() == config::kMaxExceptions);
    for (std::size_t k : keptWords) CHECK(k != 2);
}

static void wordRules()
{
    for (const char c : std::string(",.;:!?'\"-()[]{}<>/\\|@#$%^&*_+=~`"))
        CHECK(word::endsWord(static_cast<unsigned char>(c)));
    CHECK(word::endsWord(0x2116)); // №
    CHECK(!word::endsWord(' '));
    CHECK(!word::endsWord('a') && !word::endsWord('Z') && !word::endsWord('5'));
    CHECK(!word::endsWord(0x0436)); // ж
    CHECK(!word::endsWord(0x7f) && !word::endsWord(0));

    CHECK(word::caseEligible(0, 5));            // ghbdtn
    CHECK(word::caseEligible(0b1, 5));          // Ghbdtn
    CHECK(word::caseEligible(0b11111, 5));      // GHBDTN
    CHECK(!word::caseEligible(0b10, 5));        // gHbdtn
    CHECK(!word::caseEligible(0b10001, 5));     // GhbdtN
    CHECK(word::caseEligible(0b100011, 2));     // only the first two keys count: "NS,"
    CHECK(word::caseEligible(~0u, 40));         // long all-caps word
    CHECK(!word::caseEligible(0b10, 40));

    CHECK(word::form("ghbdtn", true) == "привет");
    CHECK(word::form("ghbdtn", false) == "ghbdtn");
    CHECK(word::form("ghbdtn", true, 0b1) == "Привет");
    CHECK(word::form("ns", true, 0b11) == "ТЫ");
    CHECK(word::form("", true).empty());

    CHECK(word::lettersBeforeTail("ckjdj,") == 5);
    CHECK(word::lettersBeforeTail("ns,.") == 2);
    CHECK(word::lettersBeforeTail("ckjdj") == 0);   // ends in a letter: nothing to strip
    CHECK(word::lettersBeforeTail(",.;") == 0);     // no letters
    CHECK(word::lettersBeforeTail("") == 0);
}

static void registryOncePerRevision()
{
    using namespace rasklad::policy;
    std::array<Adapter, 2> apps{{{"slack", "slack", Domain::RegularApplication, false, false, true},
                                  {"chromium", "chromium", Domain::RegularApplication, false, false, true}}};
    const Context c{"slack", "slack", "slack", Domain::RegularApplication, Identity::CompositorAppId,
                    Field::Unknown, true, 1, 2, 1};
    const Permission manual{true, "slack", Mode::Manual, 1};
    CHECK(authorizeCapture({apps.data(), apps.size(), 10}, manual, c).allowed());
    CHECK(authorizeCapture({apps.data(), apps.size(), 10}, manual, c).allowed()); // cached
    // A new snapshot (new revision) is checked again, even at the same address.
    apps[1].id = "slack";
    CHECK(!authorizeCapture({apps.data(), apps.size(), 11}, manual, c).allowed());
    CHECK(authorizeCapture({apps.data(), 1, 13}, manual, c).allowed()); // the first entry alone is valid
    apps[1].id = "chromium";
    CHECK(authorizeCapture({apps.data(), apps.size(), 12}, manual, c).allowed());
    CHECK(!authorizeCapture({apps.data(), apps.size(), 0}, manual, c).allowed()); // revision 0 never valid
    CHECK(!authorizeCapture({nullptr, 1, 12}, manual, c).allowed());
    CHECK(!authorizeCapture({apps.data(), 65, 12}, manual, c).allowed());
}

int main()
{
    configRules();
    wordRules();
    registryOncePerRevision();
    if (failures) return 1;
    std::printf("product core tests OK\n");
    return 0;
}
