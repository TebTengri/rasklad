// SYS-223 input-method-v2 wire laboratory (developer-only, not linked into the app).
// Synthetic text-field model shared by the fake server: UTF-8 byte offsets as in
// the protocol, bounded size, strict UTF-8 validation. No I/O, no real input.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace inputlab {

// Strict: reference server that honours the commit serial rule of
// input-method-unstable-v2 (commit serial must equal the number of done events).
// UncheckedModel: deliberately unchecked MODEL of the forwarding pattern read in
// public Hyprland sources (see docs/INPUT-LAB-001.md). It is not Hyprland and
// does not run Hyprland code.
enum class SerialPolicy { Strict, UncheckedModel };

// Numeric values of zwp_text_input_v3 enums referenced by input-method-v2 events.
namespace tiv3 {
inline constexpr std::uint32_t kHintNone = 0;
inline constexpr std::uint32_t kPurposeNormal = 0;
inline constexpr std::uint32_t kPurposePassword = 8;
inline constexpr std::uint32_t kCauseInputMethod = 0;
inline constexpr std::uint32_t kCauseOther = 1;
} // namespace tiv3

// Protocol limit for surrounding_text / commit_string text.
inline constexpr std::size_t kMaxProtocolTextBytes = 4000;

struct SyntheticField {
    std::string text;            // UTF-8, invented test text only
    std::uint32_t cursor = 0;    // UTF-8 byte offset; no selection in the lab
    std::uint32_t hint = tiv3::kHintNone;
    std::uint32_t purpose = tiv3::kPurposeNormal;
};

enum class EditResult { Applied, InvalidRange, InvalidUtf8, EmbeddedNul, TooLong };

[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept;
[[nodiscard]] bool isUtf8Boundary(std::string_view text, std::size_t offset) noexcept;
[[nodiscard]] bool isValidField(const SyntheticField& field) noexcept;

// Applies delete_surrounding_text(before, after) followed by commit_string, in
// the order required by zwp_input_method_v2.commit (no preedit in the lab).
// All-or-nothing: on any error the field is left unchanged.
[[nodiscard]] EditResult applyImeEdit(SyntheticField& field, std::uint32_t beforeLength,
                                      std::uint32_t afterLength, std::string_view commitText);

// Simulated user typing at the cursor. All-or-nothing.
[[nodiscard]] bool insertAtCursor(SyntheticField& field, std::string_view text);

} // namespace inputlab
