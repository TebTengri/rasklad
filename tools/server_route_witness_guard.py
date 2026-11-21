# SPDX-License-Identifier: MIT
"""Fixed public ABI admission and child-only environment for server diagnostics."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import stat

SERVER=Path('/usr/lib/libwayland-server.so.0.26.0')
SONAME=Path('/usr/lib/libwayland-server.so.0')
HYPR=Path('/usr/bin/Hyprland')

def validate(base,root,exact=False):
    if platform.machine()!='x86_64':raise base.Refused('server_route_arch')
    lock=Path(root)/'data/server-route-witness-lock.json'
    base.regular_file(lock)
    fd=os.open(lock,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
    try:
        payload=os.read(fd,16385)
        if len(payload)>16384:raise base.Refused('server_route_lock_bound')
    finally:os.close(fd)
    value=json.loads(payload)
    if (not isinstance(value,dict) or type(value.get('schema')) is not int or value.get('schema')!=1 or value.get('arch')!='x86_64'
            or not isinstance(value.get('files'),dict) or set(value['files'])!={str(SERVER),str(HYPR)}):
        raise base.Refused('server_route_lock')
    if not SONAME.is_symlink() or SONAME.resolve(strict=True)!=SERVER:
        raise base.Refused('server_route_soname')
    for path in ([SERVER,HYPR] if exact else [SERVER]):
        row=value['files'][str(path)]
        if not isinstance(row,dict) or not isinstance(row.get('sha256'),str) or not re.fullmatch('[0-9a-f]{64}',row['sha256']):
            raise base.Refused('server_route_hash')
        base.regular_file(path)
        fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
        try:
            info=os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_size>32_000_000
                    or info.st_uid!=0 or info.st_mode&0o022):
                raise base.Refused('server_route_binary_bound')
            digest=hashlib.sha256();header=os.read(fd,20);digest.update(header)
            if len(header)!=20 or header[:6]!=b'\x7fELF\x02\x01' or header[18:20]!=b'\x3e\x00':
                raise base.Refused('server_route_elf')
            total=20
            while True:
                chunk=os.read(fd,65536)
                if not chunk:break
                total+=len(chunk)
                if total>32_000_000:raise base.Refused('server_route_binary_bound')
                digest.update(chunk)
            if digest.hexdigest()!=row['sha256']:raise base.Refused('server_route_hash')
        finally:os.close(fd)
    return True

def child_env(base):
    if os.environ.get('RASKLAD_SERVER_ROUTE_WITNESS_LAB')!='1':return None
    for name in ('RASKLAD_NATIVE_WAYLAND_LAB','RASKLAD_FOCUS_STRESS_LAB','RASKLAD_SPACE_TELEMETRY_LAB',
                 'RASKLAD_ADDON_WITNESS_LAB','RASKLAD_CORE_ENTRY_WITNESS_LAB','RASKLAD_GRAB_ENTRY_WITNESS_LAB'):
        if os.environ.get(name)!='1':raise base.Refused('server_route_private_mode')
    if (os.environ.get('HOME')!='/home/lab' or os.environ.get('XDG_RUNTIME_DIR')!='/run/lab'
            or any(k in os.environ for k in ('LD_PRELOAD','DISPLAY','WAYLAND_DEBUG','RASKLAD_PARENT_FD'))
            or Path('/run/lab/server-route-witness.bin').exists()):
        raise base.Refused('server_route_child_environment')
    return dict(os.environ,LD_PRELOAD='/lab/raskladserverroute.so')
