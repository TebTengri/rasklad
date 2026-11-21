#!/usr/bin/env python3
# SPDX-License-Identifier: MIT AND BSD-3-Clause
#
# rasklad code in this file is MIT. The anchor strings inside EDITS (the
# "old" side of each pair, and the unchanged upstream lines repeated on the
# "new" side) are verbatim excerpts of Aquamarine v0.15.1 src/backend/Wayland.cpp
# and remain under Aquamarine's license; the MIT grant does not relicense them:
#
#   BSD 3-Clause License
#
#   Copyright (c) 2024, Hypr Development
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice, this
#      list of conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright notice,
#      this list of conditions and the following disclaimer in the documentation
#      and/or other materials provided with the distribution.
#
#   3. Neither the name of the copyright holder nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
#   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
#   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Apply the rasklad DMA-BUF readiness experiment to a pinned Aquamarine v0.15.1 checkout.

Only edits src/backend/Wayland.cpp of an upstream checkout that lives under build/.
Every anchor must match exactly once, otherwise nothing is written (fail closed).
Every path component below build/ (checkout, src, backend, Wayland.cpp) is opened
relative to its parent with O_NOFOLLOW, so a symlinked ancestor is refused. The
target must be a regular single-link file; it is replaced atomically by a unique
temporary file in the same validated directory, never truncated in place.
Trust model: accidental links fail safely; active same-UID races are out of scope.
The patched tree stays in ignored build/ and is never committed or installed.
Run: python3 -I tools/aquamarine_readiness_patch.py <checkout> [--check]
"""
import argparse
import hashlib
import os
import secrets
import stat
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
MARK = '[readiness]'
TARGET_DIRS = ('src', 'backend')
TARGET_NAME = 'Wayland.cpp'
MAX_SOURCE = 4 * 1024 * 1024
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
READ_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
TEMP_FLAGS = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC

INCLUDES = '''#include <poll.h>
#include <cerrno>
#include <chrono>
#include <memory>

namespace {
    // rasklad SYS-246 readiness experiment: state of the initial default-feedback batch.
    struct SRaskladFeedbackReadiness {
        bool done       = false;
        bool mainDevice = false;
        bool invalid    = false;
    };
}

bool Aquamarine::CWaylandBackend::initDmabuf() {
'''

EDITS = [
    # 1. helper state + includes, placed at namespace scope before the function.
    ('bool Aquamarine::CWaylandBackend::initDmabuf() {\n', INCLUDES),
    # 2. shared readiness state, captured by value so later feedback updates stay valid.
    ('''        return false;
    }

    waylandState.dmabufFeedback->setDone([this](CCZwpLinuxDmabufFeedbackV1* r) {
        // no-op
        backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: Got done");
''', '''        return false;
    }

    auto readiness = std::make_shared<SRaskladFeedbackReadiness>();

    waylandState.dmabufFeedback->setDone([this, readiness](CCZwpLinuxDmabufFeedbackV1* r) {
        readiness->done = true;
        backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: Got done");
'''),
    # 3. main_device: record arrival; malformed size refuses instead of ASSERT abort.
    ('''    waylandState.dmabufFeedback->setMainDevice([this](CCZwpLinuxDmabufFeedbackV1* r, wl_array* deviceArr) {
        backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: Got main device");

        dev_t device;
        ASSERT(deviceArr->size == sizeof(device));
''', '''    waylandState.dmabufFeedback->setMainDevice([this, readiness](CCZwpLinuxDmabufFeedbackV1* r, wl_array* deviceArr) {
        backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: Got main device");
        readiness->mainDevice = true;

        dev_t device;
        if (!deviceArr || !deviceArr->data || deviceArr->size != sizeof(device)) {
            readiness->invalid = true;
            backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] malformed main_device");
            return;
        }
'''),
    # 4. bounded wait for 'done' after the existing roundtrip; explicit refusals.
    ('''    wl_display_roundtrip(waylandState.display);

    if (!drmState.nodeName.empty()) {
''', '''    wl_display_roundtrip(waylandState.display);

    // One roundtrip does not prove the initial batch is complete (linux-dmabuf: parameters are
    // atomic only at 'done'). Wait on the display fd up to a monotonic deadline; never sleep.
    backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: [readiness] checking initial feedback");
    bool readinessIoError = false;
    if (!readiness->done) {
        const auto deadline  = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        const int  displayFd = wl_display_get_fd(waylandState.display);
        while (!readiness->done && !readinessIoError) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0)
                break;
            if (wl_display_prepare_read(waylandState.display) != 0) {
                readinessIoError = wl_display_dispatch_pending(waylandState.display) < 0;
                continue;
            }
            if (wl_display_flush(waylandState.display) < 0 && errno != EAGAIN) {
                wl_display_cancel_read(waylandState.display);
                readinessIoError = true;
                break;
            }
            pollfd    pfd = {.fd = displayFd, .events = POLLIN, .revents = 0};
            const int ret = poll(&pfd, 1, static_cast<int>(left));
            if (ret <= 0) {
                wl_display_cancel_read(waylandState.display);
                if (ret < 0 && errno == EINTR)
                    continue;
                readinessIoError = ret < 0;
                break;
            }
            readinessIoError = wl_display_read_events(waylandState.display) < 0 || wl_display_dispatch_pending(waylandState.display) < 0;
        }
    }

    if (readinessIoError || wl_display_get_error(waylandState.display) != 0) {
        backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] refused: display error while waiting");
        return false;
    }
    if (!readiness->done) {
        backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] refused: feedback done not received before deadline");
        return false;
    }
    if (readiness->invalid) {
        backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] refused: malformed main_device");
        return false;
    }
    if (!readiness->mainDevice) {
        backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] refused: done without main_device");
        return false;
    }
    if (drmState.nodeName.empty()) {
        backend->log(AQ_LOG_ERROR, "zwp_linux_dmabuf_v1: [readiness] refused: main_device unresolved");
        return false;
    }
    backend->log(AQ_LOG_DEBUG, "zwp_linux_dmabuf_v1: [readiness] initial feedback complete");

    if (!drmState.nodeName.empty()) {
'''),
]


class Refused(Exception):
    pass


def _walk(start_fd, names):
    """Open each name as a directory relative to the previous one, never following a symlink."""
    fd = os.dup(start_fd)
    try:
        for name in names:
            try:
                next_fd = os.open(name, DIR_FLAGS, dir_fd=fd)
            except OSError as exc:
                raise Refused('unsafe path component %r' % name) from exc
            os.close(fd)
            fd = next_fd
    except BaseException:
        os.close(fd)
        raise
    return fd


def _open_abs_dir(path):
    parts = [part for part in os.path.abspath(path).split('/') if part]
    root = os.open('/', DIR_FLAGS)
    try:
        return _walk(root, parts)
    finally:
        os.close(root)


def _checkout_parts(build, checkout):
    text = os.fspath(checkout)
    if not text or '\x00' in text or '..' in PurePosixPath(text).parts:
        raise Refused('checkout must be a real directory under build/')
    base, path = os.path.abspath(build), os.path.abspath(text)
    if path == base or os.path.commonpath([base, path]) != base:
        raise Refused('checkout must be a real directory under build/')
    return PurePosixPath(os.path.relpath(path, base)).parts


def _read_target(dir_fd):
    try:
        fd = os.open(TARGET_NAME, READ_FLAGS, dir_fd=dir_fd)
    except OSError as exc:
        raise Refused('Wayland.cpp missing or symlink') from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise Refused('Wayland.cpp is not a regular file')
        if info.st_nlink != 1:
            raise Refused('Wayland.cpp has %d links' % info.st_nlink)
        if info.st_uid != os.getuid():
            raise Refused('Wayland.cpp has a foreign owner')
        if info.st_size > MAX_SOURCE:
            raise Refused('Wayland.cpp too large')
        chunks, total = [], 0
        while total <= MAX_SOURCE:
            block = os.read(fd, MAX_SOURCE + 1 - total)
            if not block:
                break
            chunks.append(block)
            total += len(block)
    finally:
        os.close(fd)
    if total > MAX_SOURCE:
        raise Refused('Wayland.cpp too large')
    try:
        return b''.join(chunks).decode('utf-8'), info
    except UnicodeDecodeError as exc:
        raise Refused('Wayland.cpp is not UTF-8') from exc


def _replace_target(dir_fd, info, data):
    """Write a unique sibling temp file, then rename it over the target; the old inode is never written."""
    temp = '.%s.rasklad-%s.tmp' % (TARGET_NAME, secrets.token_hex(8))
    fd = os.open(temp, TEMP_FLAGS, 0o600, dir_fd=dir_fd)
    try:
        try:
            view = memoryview(data)
            while view:
                view = view[os.write(fd, view):]
            os.fchmod(fd, stat.S_IMODE(info.st_mode))
            os.fsync(fd)
        finally:
            os.close(fd)
        current = os.stat(TARGET_NAME, dir_fd=dir_fd, follow_symlinks=False)
        if (current.st_dev, current.st_ino) != (info.st_dev, info.st_ino) or current.st_nlink != 1:
            raise Refused('Wayland.cpp changed during patching')
        os.rename(temp, TARGET_NAME, src_dir_fd=dir_fd, dst_dir_fd=dir_fd)
    except BaseException:
        try:
            os.unlink(temp, dir_fd=dir_fd)
        except FileNotFoundError:
            pass
        raise
    os.fsync(dir_fd)


def patch_checkout(build, checkout, check):
    """Validate, patch and (unless check) atomically replace Wayland.cpp; returns the sha256 of the result."""
    parts = _checkout_parts(build, checkout)
    build_fd = _open_abs_dir(build)
    try:
        dir_fd = _walk(build_fd, parts + TARGET_DIRS)
    finally:
        os.close(build_fd)
    try:
        text, info = _read_target(dir_fd)
        if MARK in text:
            raise Refused('already patched')
        for old, new in EDITS:
            if text.count(old) != 1:
                raise Refused('anchor count %d for %r' % (text.count(old), old.splitlines()[0][:60]))
            text = text.replace(old, new)
        data = text.encode('utf-8')
        if not check:
            _replace_target(dir_fd, info, data)
    finally:
        os.close(dir_fd)
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout')
    parser.add_argument('--check', action='store_true', help='verify anchors only; write nothing')
    args = parser.parse_args()
    try:
        digest = patch_checkout(ROOT / 'build', args.checkout, args.check)
    except Refused as exc:
        raise SystemExit('refused: %s' % exc) from None
    print('ok edits=%d check=%s sha256=%s' % (len(EDITS), args.check, digest))


if __name__ == '__main__':
    main()
