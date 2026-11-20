"""Supervised-session injector tables: fixed scripts over the private keys helper.

Pure module (no I/O). The helper (tests/wayland_ime/keys.cpp) only knows fixed US
sequences; this layer restricts the live session to two fully fixed scripts. The
cleanup BackSpace count is part of the script and must equal the committed text
reported by the addon receipt; a mismatch revokes instead of guessing.
"""
import unicodedata

# Exact bytes written to the helper; nothing else can be written.
WIRE = {'word': b'word\n', 'space': b'space\n', 'hello': b'hello\n', 'backspace': b'backspace\n'}
ALLOWED = frozenset(WIRE)
# Helper commands that must never be sent live.
FORBIDDEN = frozenset({'undo', 'escape', 'lab-off', 'lab-on', 'xspace', 'sample2', 'mixed', 'long'})
EXPECTED = {'correction': 'привет ', 'english': 'hello '}
TYPED = {'correction': ('word', 'space'), 'english': ('hello',)}
MAX_BACKSPACE = 16


class Refused(Exception):
    pass


def graphemes(text):
    """Character count for the fixed expected texts: ASCII/Cyrillic letters and plain space only."""
    if type(text) is not str or not text:
        raise Refused('grapheme-unsupported')
    for c in text:
        cyrillic = 0x400 <= ord(c) <= 0x4ff
        if not ((c.isascii() or cyrillic) and (unicodedata.category(c) in ('Ll', 'Lu') or c == ' ')):
            raise Refused('grapheme-unsupported')
    return len(text)


def receipt(case):
    """Addon receipt record expected after the typing commands: kind, code-point count and
    commit sequence 01 (the session's only commit), no text."""
    if type(case) is not str or case not in TYPED:
        raise Refused('case')
    kind = b'c' if case == 'correction' else b'o'
    return kind + b'%02d01\n' % graphemes(EXPECTED[case])


def script(case):
    """Full fixed command sequence for one case: typing, then exact cleanup."""
    if type(case) is not str or case not in TYPED:
        raise Refused('case')
    count = graphemes(EXPECTED[case])
    if count > MAX_BACKSPACE:
        raise Refused('cleanup-count')
    return list(TYPED[case]) + ['backspace'] * count
