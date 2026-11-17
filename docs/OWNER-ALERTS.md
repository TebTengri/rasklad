# Development owner alerts

Owner requested visible/sound alerts when their action is required or a task
actually finishes. This helper is development workflow, not an installed product
feature. No daemon, polling, autostart, account messages or desktop config edits.

`bin/notify-owner` defaults to plan only; explicit --test/--needs-user/--done sends
one fixed generic notification. Omarchy normal notifications expire even with
requested timeout0; critical remains visible until closed. Keep app-name=rasklad
so the shell's Do Not Disturb policy is respected rather than bypassed.

Sound runs only on confirmed DNDoff. Two optional owner-local files under ignored
build/owner-alert-sound-1.mp3 and -2.mp3 alternate. Audio files, downloaded source
media and provenance are NEVER committed/distributed. No audio network requests
at notification time. The published helper contains no game audio.

Private directory must belong to owner and not be group/world writable. Audio is
opened with O_NOFOLLOW, verified regular, owner-only identity and <=512KiB; an open
fd is passed to pw-play to keep the selected inode fixed. No persistent volume
change or unmute: attenuation applies only to the player stream. Playback capped
at4s. Stock installed completion cue is fallback if no local clip is available.

Rotation state/lock is ignored build/owner-alert-sequence, a short numeric file.
Nonblocking flock avoids overlapping audio, rejects unsafe state files, advances
only after successful playback. Failure never loops or blocks project work.
Four local mock tests verify1→2→1 persistence, failure/no-advance, symlink refusal
and concurrent suppression; no audio device or user input in those tests.
