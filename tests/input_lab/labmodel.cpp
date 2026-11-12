#include "labmodel.h"

namespace inputlab {

bool isValidUtf8(std::string_view text) noexcept
{
    const std::size_t size = text.size();
    std::size_t i = 0;
    while (i < size) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            codePoint = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            codePoint = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            codePoint = lead & 0x07u;
        } else {
            return false; // continuation byte, C0/C1 overlong lead or > U+10FFFF lead
        }
        if (size - i < length)
            return false;
        for (std::size_t k = 1; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && codePoint < 0x800u) || (length == 4 && codePoint < 0x10000u)
            || codePoint > 0x10FFFFu || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
            return false;
        i += length;
    }
    return true;
}

bool isUtf8Boundary(std::string_view text, std::size_t offset) noexcept
{
    if (offset > text.size())
        return false;
    if (offset == text.size())
        return true;
    return (static_cast<unsigned char>(text[offset]) & 0xC0u) != 0x80u;
}

bool isValidField(const SyntheticField& field) noexcept
{
    return field.text.size() <= kMaxProtocolTextBytes
        && field.text.find('\0') == std::string::npos && isValidUtf8(field.text)
        && field.cursor <= field.text.size() && isUtf8Boundary(field.text, field.cursor);
}

EditResult applyImeEdit(SyntheticField& field, std::uint32_t beforeLength,
                        std::uint32_t afterLength, std::string_view commitText)
{
    if (!isValidField(field))
        return EditResult::InvalidRange;

    const std::size_t cursor = field.cursor;
    const std::size_t size = field.text.size();
    // Compare without forming cursor + after (no overflow).
    if (beforeLength > cursor || afterLength > size - cursor)
        return EditResult::InvalidRange;

    const std::size_t start = cursor - beforeLength;
    const std::size_t end = cursor + afterLength;
    if (!isUtf8Boundary(field.text, start) || !isUtf8Boundary(field.text, end))
        return EditResult::InvalidRange;

    if (commitText.find('\0') != std::string_view::npos)
        return EditResult::EmbeddedNul;
    if (!isValidUtf8(commitText))
        return EditResult::InvalidUtf8;
    if (commitText.size() > kMaxProtocolTextBytes
        || size - (end - start) + commitText.size() > kMaxProtocolTextBytes)
        return EditResult::TooLong;

    field.text.replace(start, end - start, commitText.data(), commitText.size());
    field.cursor = static_cast<std::uint32_t>(start + commitText.size());
    return EditResult::Applied;
}

bool insertAtCursor(SyntheticField& field, std::string_view text)
{
    if (!isValidField(field) || text.find('\0') != std::string_view::npos || !isValidUtf8(text)
        || text.size() > kMaxProtocolTextBytes - field.text.size())
        return false;
    field.text.insert(field.cursor, text.data(), text.size());
    field.cursor = static_cast<std::uint32_t>(field.cursor + text.size());
    return true;
}

} // namespace inputlab
