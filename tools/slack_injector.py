"""Supervised-session injector: fixed command allowlist for the private keys helper.

Pure module (no I/O). The helper itself only knows fixed US sequences; this layer
further restricts the live script to two cases and BackSpace cleanup, and requires
a valid single-use token from the supervisor before every command.
"""

# Live script cases (owner-approved design): one correction, one English word unchanged.
CASES = {
    'correction': (('word', 6), ('space', 1)),   # types "ghbdtn" + Space; expected commit "привет "
    'english': (('hello', 6),),                  # types "hello "; expected unchanged
}
ALLOWED = frozenset({'word', 'space', 'hello', 'backspace'})
# Never sent live: undo/escape/lab-off/lab-on/xspace/sample2/mixed/long and anything unknown.
MAX_BACKSPACE = 16
EXPECTED = {'correction': 'привет ', 'english': 'hello '}


class Refused(Exception):
    pass


def graphemes(text):
    """Count user-visible characters for the fixed expected texts (Cyrillic/Latin, no combining marks)."""
    if any(0x300 <= ord(c) <= 0x36f or ord(c) < 0x20 or ord(c) == 0x7f for c in text):
        raise Refused('grapheme-unsupported')
    return len(text)


def case_commands(case):
    if case not in CASES:
        raise Refused('case')
    return [name for name, _ in CASES[case]]


def cleanup_commands(committed_count):
    """BackSpace by committed grapheme count reported by the addon receipt, bounded."""
    if type(committed_count) is not int or not 0 <= committed_count <= MAX_BACKSPACE:
        raise Refused('cleanup-count')
    return ['backspace'] * committed_count


def encode(command, token, supervisor):
    """One helper line, only if the command is allowed and the supervisor accepts the token."""
    if command not in ALLOWED:
        raise Refused('command')
    if not supervisor.consume(token):
        raise Refused('token')
    return (command + '\n').encode('ascii')
