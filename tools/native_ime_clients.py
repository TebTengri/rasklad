#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared native Chromium/Fcitx fixture; called only inside a private lab namespace."""
import json
import os
from pathlib import Path
import subprocess
import time


def input_frontend_absent(returncode, output, error):
    if returncode == 0:
        return b'org.fcitx.Fcitx.InputMethod1' not in output
    return (b'org.freedesktop.DBus.Error.UnknownObject' in error
            or (b'org.freedesktop.DBus.Error.UnknownMethod' in error
                and b'Object does not exist' in error))


def run(base, children):
    if (os.environ.get('RASKLAD_NATIVE_WAYLAND_LAB') != '1'
            or os.environ.get('HOME') != '/home/lab'
            or os.environ.get('XDG_RUNTIME_DIR') != '/run/lab'
            or os.environ.get('WAYLAND_DISPLAY') != 'wayland-lab'
            or os.environ.get('DISPLAY') or os.environ.get('WAYLAND_SOCKET')
            or os.environ.get('RASKLAD_PARENT_FD')):
        raise base.Refused('private_native_clients_required')
    fcitx=subprocess.Popen(['/usr/bin/fcitx5','-D','-k','--disable','all','--enable','keyboard,dbus,wayland,waylandim,raskladlab','--verbose','*=0'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(fcitx)
    end=time.monotonic()+4
    while time.monotonic()<end:
     if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
     p=subprocess.run(['/usr/bin/gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus','--method','org.freedesktop.DBus.NameHasOwner','org.fcitx.Fcitx5'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=.3)
     if p.stdout==b'(true,)\n':break
     time.sleep(.03)
    else:raise base.Refused('fcitx_timeout')
    factory=subprocess.run(['/usr/bin/gdbus','call','--session','--dest','org.fcitx.Fcitx5','--object-path','/inputmethod','--method','org.freedesktop.DBus.Introspectable.Introspect'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=.5)
    dbus_input_absent=input_frontend_absent(factory.returncode,factory.stdout,factory.stderr)
    if fcitx.poll() is not None:raise base.Refused('fcitx_exited')
    if not dbus_input_absent:raise base.Refused('unexpected_dbus_input_frontend')
    p=subprocess.run(['/usr/bin/node','/lab/driver.mjs'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,timeout=50,env=dict(os.environ,RASKLAD_OWN_FCITX_PID=str(fcitx.pid),RASKLAD_CLK_TCK=str(os.sysconf('SC_CLK_TCK'))))
    if len(p.stdout)>32768:raise base.Refused('driver_output_bound')
    r=json.loads(p.stdout);r['driverExitCode']=p.returncode;r['dbusInputFrontendAbsent']=dbus_input_absent;r['passed']=p.returncode==0 and r.get('passed') is True and dbus_input_absent;return r
