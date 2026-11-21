"""Supervised-session injector tables: fixed scripts over the private keys helper.

Pure module (no I/O). The helper (tests/wayland_ime/keys.cpp) only knows fixed US
sequences; this layer restricts the live session to two fully fixed scripts. The
cleanup BackSpace count is part of the script and must equal the committed text
reported by the addon receipt; a mismatch revokes instead of guessing.
"""
import unicodedata

# Exact bytes written to the helper; nothing else can be written.
WIRE = {'word': b'word\n', 'space': b'space\n', 'hello': b'hello\n', 'backspace': b'backspace\n',
        'shift2': b'shift2\n'}  # SYS-225 double Shift; the helper accepts it only with RASKLAD_MANUAL_LAB=1
ALLOWED = frozenset(WIRE)
# Helper commands that must never be sent live.
FORBIDDEN = frozenset({'alt1', 'alt2', 'altr2', 'alttab', 'capword', 'shortword', 'hyphen', 'rugroup', 'usgroup', 'wordcomma', 'exclaim', 'enter', 'undo', 'escape', 'lab-off', 'lab-on', 'xspace', 'sample2', 'mixed', 'long',
                       'shiftslow', 'shifta', 'shiftr2', 'ctrlshift2', 'shiftlr2', 'zhurnal', 'khorosho', 'yolka', 'hellocomma'})  # SYS-225 offline manual suite only
# EXPECTED: field content after typing (cleanup removes exactly this many characters).
# COMMIT: the addon's single commit (its receipt carries this length).
# The English case starts with a raw space: after the previous case's BackSpaces the addon
# bypasses until a boundary (never corrects a suffix of a passed-through token), and the
# space is that boundary. Live run 002 showed 'hello' bypassing without it.
EXPECTED = {'correction': 'привет ', 'english': ' hello ', 'manual': 'привет ', 'manualafter': ' привет '}
COMMIT = {'correction': 'привет ', 'english': 'hello '}
TYPED = {'correction': ('word', 'space'), 'english': ('space', 'hello'),
         # SYS-225 (manual addon only): double Shift before Space; double Shift after Space.
         'manual': ('word', 'shift2', 'space'), 'manualafter': ('space', 'word', 'space', 'shift2')}
# Receipt checkpoints: (commands sent so far, kind, committed text). c = converted commit,
# o = original commit, w = replacement of the addon's own last word (delete + one commit;
# not r, which is the addon's context-reset record).
CHECKPOINTS = {'correction': ((2, b'c', 'привет '),), 'english': ((2, b'o', 'hello '),),
               'manual': ((3, b'c', 'привет '),),
               'manualafter': ((3, b'o', 'ghbdtn '), (4, b'w', 'привет '))}
MANUAL_CASES = frozenset({'manual', 'manualafter'})
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


def receipts(case, sequence=1):
    """Ordered receipt checkpoints: (commands sent, exact record). Records carry kind, code-point
    count and the addon's commit sequence (1 = the session's first commit), never text."""
    if type(case) is not str or case not in TYPED:
        raise Refused('case')
    points = CHECKPOINTS[case]
    if type(sequence) is not int or not 1 <= sequence or sequence + len(points) - 1 > 9:
        raise Refused('sequence')
    return [(sent, kind + b'%02d%02d\n' % (graphemes(text), sequence + i))
            for i, (sent, kind, text) in enumerate(points)]


def receipt(case, sequence=1):
    """The single receipt of a one-checkpoint case."""
    points = receipts(case, sequence)
    if len(points) != 1:
        raise Refused('case')
    return points[0][1]


def script(case):
    """Full fixed command sequence for one case: typing, then exact cleanup."""
    if type(case) is not str or case not in TYPED:
        raise Refused('case')
    count = graphemes(EXPECTED[case])
    if count > MAX_BACKSPACE:
        raise Refused('cleanup-count')
    return list(TYPED[case]) + ['backspace'] * count
