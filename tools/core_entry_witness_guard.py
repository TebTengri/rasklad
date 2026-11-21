# SPDX-License-Identifier: MIT
"""Outer-only ABI admission for the opt-in Fcitx core-entry diagnostic shim."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import stat

ROOT=Path(__file__).resolve().parent.parent
CORE_PATH=Path('/usr/lib/libFcitx5Core.so.5.1.23')
FRONTEND_PATH=Path('/usr/lib/fcitx5/libwaylandim.so')
SONAME_PATH=Path('/usr/lib/libFcitx5Core.so.7')
WAYLAND_PATH=Path('/usr/lib/libwayland-client.so.0.26.0')
WAYLAND_SONAME_PATH=Path('/usr/lib/libwayland-client.so.0')
MAX_LOCK_BYTES=16384
MAX_LIBRARY_BYTES=5_000_000


def _read_bounded(base,path,max_bytes):
    base.regular_file(path)
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
    try:
        metadata=os.fstat(fd)
        if (not stat.S_ISREG(metadata.st_mode) or metadata.st_size>max_bytes
                or metadata.st_mode&0o022 or metadata.st_uid not in (0,os.getuid())):
            raise base.Refused('core_entry_file_bound')
        chunks=[];total=0
        while True:
            chunk=os.read(fd,min(65536,max_bytes+1-total))
            if not chunk:break
            chunks.append(chunk);total+=len(chunk)
            if total>max_bytes:raise base.Refused('core_entry_file_bound')
        return b''.join(chunks)
    finally:os.close(fd)


def validate_core_entry(base,root=ROOT):
    """Read only fixed public runtime binaries; never execute or load the shim."""
    lock=json.loads(_read_bounded(base,Path(root)/'data/core-entry-witness-lock.json',MAX_LOCK_BYTES))
    if (not isinstance(lock,dict) or type(lock.get('schema')) is not int or lock['schema']!=1
            or lock.get('arch')!='x86_64' or platform.machine()!='x86_64'):
        raise base.Refused('core_entry_abi_lock')
    expected={str(CORE_PATH),str(FRONTEND_PATH),str(WAYLAND_PATH)}
    if not isinstance(lock.get('files'),dict) or set(lock['files'])!=expected:
        raise base.Refused('core_entry_abi_paths')
    if not SONAME_PATH.is_symlink() or SONAME_PATH.resolve(strict=True)!=CORE_PATH:
        raise base.Refused('core_entry_soname')
    if not WAYLAND_SONAME_PATH.is_symlink() or WAYLAND_SONAME_PATH.resolve(strict=True)!=WAYLAND_PATH:
        raise base.Refused('core_entry_wayland_soname')
    for path in (CORE_PATH,FRONTEND_PATH,WAYLAND_PATH):
        row=lock['files'][str(path)]
        if (not isinstance(row,dict) or not isinstance(row.get('sha256'),str)
                or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
            raise base.Refused('core_entry_abi_hash')
        payload=_read_bounded(base,path,MAX_LIBRARY_BYTES)
        if hashlib.sha256(payload).hexdigest()!=row['sha256']:
            raise base.Refused('core_entry_abi_hash')
        # ELFCLASS64, little endian, EM_X86_64. Hash pins remain authoritative.
        if len(payload)<20 or payload[:6]!=b'\x7fELF\x02\x01' or payload[18:20]!=b'\x3e\x00':
            raise base.Refused('core_entry_abi_arch')
    return True
