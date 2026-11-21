#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Owned synthetic DMA-BUF parent for installed Hyprland; no GPU or host display."""
import argparse
import array
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
INSIDE = Path(__file__) == Path('/lab/probe.py')
spec = importlib.util.spec_from_file_location('nested', Path('/lab/nested.py') if INSIDE else ROOT/'tools/hyprland_nested_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
GLOBALS = [('wl_compositor', 6), ('wl_seat', 9), ('wl_shm', 1), ('xdg_wm_base', 6), ('zwp_linux_dmabuf_v1', 4)]
MARKERS = {'mainDeviceSeen': b'zwp_linux_dmabuf_v1: Got main device',
           'deviceResolutionFailed': b'drmGetDeviceFromDevId failed',
           'feedbackDone': b'zwp_linux_dmabuf_v1: Got done',
           'nodeOpened': b'zwp_linux_dmabuf_v1: opened node',
           'allocatorUnavailable': b'no allocator available',
           'waylandCannotStart': b'Wayland backend cannot start',
           # Emitted only by tools/aquamarine_readiness_patch.py; proves the patched library loaded.
           'patchLoaded': b'[readiness] checking initial feedback',
           'readinessComplete': b'[readiness] initial feedback complete',
           'refusedDeadline': b'[readiness] refused: feedback done not received before deadline',
           'refusedNoMainDevice': b'[readiness] refused: done without main_device',
           'refusedUnresolved': b'[readiness] refused: main_device unresolved',
           'refusedMalformed': b'[readiness] refused: malformed main_device',
           'refusedDisplayError': b'[readiness] refused: display error while waiting'}
REFUSALS = ('refusedDeadline', 'refusedNoMainDevice', 'refusedUnresolved', 'refusedMalformed', 'refusedDisplayError')
MODES = ('immediate', 'deferred', 'never')
AQ_LIB = ROOT/'build/aquamarine-readiness/lib'


def message(obj, opcode, payload=b''):
    return struct.pack('II', obj, ((8 + len(payload)) << 16) | opcode) + payload


def string(value):
    raw = value.encode('ascii') + b'\0'
    return struct.pack('I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


def wl_array(raw):
    return struct.pack('I', len(raw)) + raw + b'\0' * (-len(raw) % 4)


def serve(server, mode, counts, flags):
    objects, pending, delayed = {1: 'display'}, b'', []
    def allocate(ident, kind):
        if not 2 <= ident < 0xff000000 or ident in objects or len(objects) >= 128:
            raise ValueError('object_bound')
        objects[ident] = kind
    def feedback(ident):
        # Linux dev_t is 64 bit. No /sys or /dev/dri is mounted in the child.
        device = wl_array(struct.pack('Q', 0xffffffffffffffff))
        fd = os.memfd_create('owned-feedback', os.MFD_CLOEXEC)
        try:
            os.write(fd, struct.pack('IIQ', 0x34325258, 0, 0))  # XRGB8888, linear
            server.sendmsg([message(ident, 1, struct.pack('I', 16))],
                           [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [fd]))])
        finally:
            os.close(fd)
        server.sendall(message(ident, 2, device) + message(ident, 4, device) +
                       message(ident, 5, wl_array(struct.pack('H', 0))) +
                       message(ident, 6, struct.pack('I', 0)) + message(ident, 3) + message(ident, 0))
        counts['feedbackSent'] += 1
    server.settimeout(8)
    try:
        for _ in range(512):
            block, ancillary, _, _ = server.recvmsg(4096, socket.CMSG_SPACE(256 * 4))
            for level, kind, raw in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    fds = array.array('i'); fds.frombytes(raw[:len(raw) // 4 * 4])
                    for fd in fds: os.close(fd)
            if ancillary: raise ValueError('unexpected_fds')
            if not block: break
            pending += block
            while len(pending) >= 8:
                obj, word = struct.unpack_from('II', pending)
                length, opcode = word >> 16, word & 65535
                if length < 8 or length > 4096 or length % 4: raise ValueError('frame_bound')
                if len(pending) < length: break
                frame, pending = pending[:length], pending[length:]
                counts['frames'] += 1
                if counts['frames'] > 512: raise ValueError('frame_count')
                kind = objects.get(obj)
                if kind == 'display' and opcode == 1 and length == 12:
                    ident, = struct.unpack_from('I', frame, 8); allocate(ident, 'registry')
                    counts['getRegistry'] += 1
                    for name, (interface, version) in enumerate(GLOBALS, 1):
                        server.sendall(message(ident, 0, struct.pack('I', name) + string(interface) + struct.pack('I', version)))
                elif kind == 'display' and opcode == 0 and length == 12:
                    callback, = struct.unpack_from('I', frame, 8); allocate(callback, 'callback')
                    counts['sync'] += 1
                    server.sendall(message(callback, 0, struct.pack('I', 0)) + message(1, 1, struct.pack('I', callback)))
                    del objects[callback]
                    for ident in delayed: feedback(ident)
                    delayed.clear()
                elif kind == 'registry' and opcode == 0 and length >= 24:
                    name, size = struct.unpack_from('II', frame, 8)
                    end = 16 + size; padded = 16 + (size + 3) // 4 * 4
                    if not 1 <= name <= 5 or size > 64 or padded + 8 != length: raise ValueError('bind_shape')
                    interface, maximum = GLOBALS[name - 1]
                    if frame[16:end] != interface.encode() + b'\0': raise ValueError('bind_interface')
                    version, ident = struct.unpack_from('II', frame, padded)
                    if not 1 <= version <= maximum: raise ValueError('bind_version')
                    allocate(ident, interface); counts[interface + 'Binds'] += 1
                    counts[interface + 'Version'] = version
                elif kind == 'zwp_linux_dmabuf_v1' and opcode == 2 and length == 12:
                    ident, = struct.unpack_from('I', frame, 8); allocate(ident, 'feedback')
                    counts['getDefaultFeedback'] += 1
                    # deferred: whole batch only after the NEXT sync's callback done (legal: the
                    # spec bounds no latency; batch is atomic at 'done'). never: batch withheld.
                    if mode == 'immediate': feedback(ident)
                    elif mode == 'deferred': delayed.append(ident)
                    else: counts['feedbackWithheld'] += 1
                elif kind == 'wl_compositor' and opcode in (0, 1) and length == 12:
                    ident, = struct.unpack_from('I', frame, 8); allocate(ident, 'surface' if opcode == 0 else 'region')
                elif kind == 'xdg_wm_base' and opcode == 2 and length == 16:
                    ident, = struct.unpack_from('I', frame, 8); allocate(ident, 'xdg_surface')
                elif kind == 'xdg_surface' and opcode == 1 and length == 12:
                    ident, = struct.unpack_from('I', frame, 8); allocate(ident, 'xdg_toplevel')
                elif kind in ('feedback', 'zwp_linux_dmabuf_v1', 'wl_seat', 'xdg_wm_base', 'surface', 'region', 'xdg_surface', 'xdg_toplevel'):
                    counts['other'] += 1
                else: raise ValueError('unexpected_request')
        else: raise ValueError('read_bound')
    except (BrokenPipeError, ConnectionResetError):
        flags['peerClosed'] = True
    except Exception:
        flags['serverError'] = True
    finally:
        server.close()


def inside():
    if not INSIDE or os.environ.get('RASKLAD_NESTED_PROBE') != '1' or os.environ.get('HOME') != '/home/lab': raise ValueError('private_only')
    fd = int(os.environ.pop('RASKLAD_PARENT_FD'))
    if fd < 3 or fd > 1023: raise ValueError('fd_bound')
    env = dict(os.environ, WAYLAND_SOCKET=str(fd)); os.chmod('/run/lab', 0o700)
    override = env.pop('RASKLAD_AQ_LIB', None)
    if override is not None:
        if override != '/lab/aq': raise ValueError('aq_lib_path')
        env['LD_LIBRARY_PATH'] = override  # Hyprland child only; python itself unaffected
    with tempfile.TemporaryFile() as log:
        child = subprocess.Popen(['/usr/bin/Hyprland', '--config', '/lab/hypr.conf'], env=env,
                                 pass_fds=(fd,), stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        os.close(fd)
        try: child.wait(timeout=7)
        except subprocess.TimeoutExpired: child.kill(); child.wait()
        log.seek(0); raw = log.read((4 << 20) + 1)
    flags = {key: marker in raw for key, marker in MARKERS.items()}
    refusal = [raw.find(MARKERS[key]) for key in REFUSALS if flags[key]]
    allocator = raw.find(MARKERS['allocatorUnavailable'])
    # Explicit refusal must precede any generic allocator abort to count as fail-closed.
    flags['refusalBeforeAllocator'] = bool(refusal) and (allocator < 0 or min(refusal) < allocator)
    return dict(phase=1, childExit=child.returncode, logBoundExceeded=len(raw) > 4 << 20,
                aquamarineOverride=override is not None, **flags)


def admitted_aq_lib():
    """Patched build output only: fixed ignored path, real dir, libaquamarine.so* entries only."""
    if AQ_LIB.is_symlink() or not AQ_LIB.is_dir(): raise ValueError('aq_lib_missing')
    regular = 0
    for entry in AQ_LIB.iterdir():
        if not entry.name.startswith('libaquamarine.so'): raise ValueError('aq_lib_entry')
        if entry.is_symlink():
            if '/' in os.readlink(entry) or not (AQ_LIB/os.readlink(entry)).is_file(): raise ValueError('aq_lib_link')
        elif entry.is_file(): regular += 1
        else: raise ValueError('aq_lib_entry')
    if regular != 1: raise ValueError('aq_lib_count')
    return AQ_LIB


def run(mode, logs_enabled, aq_lib=None):
    if mode not in MODES: raise ValueError('mode')
    server, client = socket.socketpair()
    counts = dict(getRegistry=0, sync=0, getDefaultFeedback=0, feedbackSent=0, feedbackWithheld=0, frames=0, other=0)
    for interface, _ in GLOBALS:
        counts[interface + 'Binds'] = 0; counts[interface + 'Version'] = 0
    flags = dict(serverError=False, peerClosed=False)
    thread = threading.Thread(target=serve, args=(server, mode, counts, flags), daemon=True); thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='rasklad-feedback-', dir='/tmp') as tmp:
            directory = Path(tmp)
            config = probe.CONFIG.replace('disable_logs = false', 'disable_logs = ' + ('false' if logs_enabled else 'true'))
            (directory/'hypr.conf').write_text(config)
            for key, value in {'passwd': f'lab:x:{os.getuid()}:{os.getgid()}:Lab:/home/lab:/usr/bin/nologin\n',
                               'group': f'lab:x:{os.getgid()}:\n', 'machine-id': '00000000000000000000000000000006\n'}.items():
                (directory/key).write_text(value)
            args = probe.sandbox_args(directory, client.fileno())
            at = args.index('--dev-bind')
            if args.count('--dev-bind') != 1 or args[at+1:at+3] != ['/dev/dri/renderD128'] * 2: raise ValueError('device_plan')
            del args[at:at+3]
            at = args.index(str(ROOT/'tools/hyprland_nested_probe.py')); args[at] = str(Path(__file__).resolve())
            at = args.index('--'); args[at:at] = ['--ro-bind', str(ROOT/'tools/hyprland_nested_probe.py'), '/lab/nested.py']
            if aq_lib is not None:
                at = args.index('--'); args[at:at] = ['--ro-bind', str(aq_lib), '/lab/aq', '--setenv', 'RASKLAD_AQ_LIB', '/lab/aq']
            with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as errors:
                result = subprocess.run(args, pass_fds=(client.fileno(),), stdout=out, stderr=errors,
                                        timeout=12, env={'PATH': '/usr/bin'}, preexec_fn=probe.limits)
                out.seek(0); raw = out.read(4097)
            if len(raw) > 4096: raise ValueError('output_bound')
            report = json.loads(raw)
            client.close(); thread.join(2)
            if thread.is_alive(): raise ValueError('server_timeout')
            return dict(mode=mode, loggingEnabled=logs_enabled, runnerExit=result.returncode,
                        counts=counts, **flags, **report)
    finally:
        client.close(); thread.join(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true'); parser.add_argument('--inside', action='store_true')
    parser.add_argument('--logs-disabled', action='store_true')
    parser.add_argument('--aquamarine-lib', action='store_true',
                        help='LD_LIBRARY_PATH the patched build/aquamarine-readiness/lib into the Hyprland child only')
    args = parser.parse_args()
    if args.inside: return inside()
    if not args.run: return dict(schema=2, planOnly=True, hostDisplay=False, gpu=False)
    if os.geteuid() == 0: raise ValueError('root_not_admitted')
    aq_lib = admitted_aq_lib() if args.aquamarine_lib else None
    return dict(schema=2, hostDisplay=False, gpu=False, aquamarineOverride=aq_lib is not None,
                variants=[run(mode, not args.logs_disabled, aq_lib) for mode in MODES])


if __name__ == '__main__':
    try:
        print(json.dumps(main(), sort_keys=True))
    except Exception:
        print(json.dumps(dict(schema=1, ok=False, controlError=True)))
        raise SystemExit(1)
