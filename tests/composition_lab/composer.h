#pragma once
#include "decisionrouter.h"

namespace rasklad::composition {

// Developer prototype only. A single owner feeds keys for one already-approved
// normal field. No platform I/O, focus detection, timers, history or logging.
struct Update {
    QString commit;
    QString preedit;
    bool corrected = false;
    bool handled = true;
};

class Composer final {
public:
    static constexpr qsizetype kMaxBuffered = routing::kMaxRouteKeys;
    Composer(routing::InputLayout layout, const routing::EvidenceSource *source)
        : m_layout(layout), m_source(source) {}
    [[nodiscard]] Update text(char32_t character);
    [[nodiscard]] Update backspace();
    // Finalize owned preedit literally before passing navigation/shortcuts through.
    [[nodiscard]] Update finishLiteral();
    void reset();
    [[nodiscard]] qsizetype buffered() const { return m_word.size(); }
private:
    routing::InputLayout m_layout;
    const routing::EvidenceSource *m_source; // borrowed, same owner thread
    QString m_word;
    bool m_literalUntilSpace = false;
};
} // namespace rasklad::composition
