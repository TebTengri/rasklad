# Mapping the hypothetical acknowledgment to the existing protocol

This is source inspection, not a transport fix or an interoperability result.
Pinned versions: Fcitx5.1.23 commit27cca6e0239938b2d53937061d0980c087856b22;
Hyprland commit efb50993780079460b0cbed1363e2166a2de1d9f.

| Needed guarantee | Inspected source/contract | Status |
| --- | --- | --- |
| Client saw current state | Input-method commit serial reflects done count | Potential signal, not a target/grab acknowledgment by itself |
| Unconditional readiness reply | Fcitx done handler requests forced preedit update only under realFocus; updatePreeditDelegate returns without realFocus | No unconditional acknowledgment established |
| Replacement grab before reply | Activated done handler creates grab before forced preedit update; updatePreeditDelegate eventually commits | Source-order hypothesis for pinned Fcitx only, not a runtime or other-IME guarantee |
| Sender validates reply against latest done | Inspected Hyprland setCommit callback accepts serial but does not use it in its body | Missing in that callback; any change needs protocol-correct stale-state handling |
| Event bound to target/grab incarnation | Current grab-key event carries serial/time/key/state; screen adds identities and ack binding | No direct mapping proven |
| No input duplicated after forwarding | Grab protocol forbids further processing an event already forwarded | Late compositor replay cannot serve as a generic recovery path |
| Missing readiness reply has bounded lossless outcome | Screen retains queued input and remains awaiting ack | Unresolved; pending is not success or cleanup |

Relevant local sources: `src/frontend/waylandim/waylandimserverv2.cpp` in the
pinned Fcitx archive; pinned `InputMethodV2.cpp` and `InputManager.cpp` snapshot;
`tests/input_lab/protocol/input-method-unstable-v2.xml`. No upstream code changed.

Do not replace the screen's explicit acknowledgment with any observed commit and
claim compatibility. A cooperative, versioned capability and client/server state
ordering may be required. Whether existing requests suffice for a pinned
Fcitx-specific adapter is a hypothesis for a future closed real-client test.
Do not gate every IME's seat on an acknowledgment it is not obliged to send.
Activation-only gating also cannot distinguish a new active state with an old
grab. Keymap/modifier coherence and held-state cancellation remain separate.

The screening result now separates three accepted-delivery cases, six explicit
rejections and one indefinitely pending case. All are expected screen outcomes;
only the first three claim delivery in the symbolic model. The missing-ack case
prevents a successful safety screen from being mistaken for progress/liveness.
