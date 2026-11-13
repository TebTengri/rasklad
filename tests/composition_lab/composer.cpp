#include "composer.h"
namespace rasklad::composition {
Update Composer::text(char32_t character)
{
    Update out;
    if (!routing::sourceLayoutOf(m_layout) || character < 0x20 || character == 0x7f || character > 0x10ffff
        || (character >= 0xd800 && character <= 0xdfff)) {
        out.handled = false;
        return out;
    }
    if (character == U' ') {
        if (!m_literalUntilSpace && !m_word.isEmpty()) {
            const routing::RouteRequest request{routing::FieldContext::Prose, m_layout, QStringView(m_word)};
            const auto decision = routing::route(request, routing::RouteProfile::SafeConfirmed, m_source);
            out.corrected = decision.action == routing::RouteAction::Correct && !decision.candidate.isEmpty();
            out.commit = out.corrected ? decision.candidate : m_word;
        }
        out.commit += QLatin1Char(' ');
        reset();
        return out;
    }
    const char32_t c = character;
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= 0x410 && c <= 0x44f) || c == 0x401 || c == 0x451;
    // Unsupported characters and long words are committed literally. Never
    // truncate or guess punctuation/compose/surrogate input in this prototype.
    if (m_literalUntilSpace || !letter || m_word.size() == kMaxBuffered) {
        out.commit = m_word + QString::fromUcs4(&character, 1);
        m_word.clear();
        m_literalUntilSpace = true;
        return out;
    }
    m_word += QChar(static_cast<char16_t>(character));
    out.preedit = m_word;
    return out;
}
Update Composer::backspace()
{
    Update out;
    if (m_literalUntilSpace || m_word.isEmpty()) {
        out.handled = false; // the adapter must handle the original key
        return out;
    }
    m_word.chop(1);
    out.preedit = m_word;
    return out;
}
Update Composer::finishLiteral()
{
    Update out;
    out.commit = m_word;
    reset();
    return out;
}
void Composer::reset()
{
    m_word.clear();
    m_literalUntilSpace = false;
}
} // namespace rasklad::composition
