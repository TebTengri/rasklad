#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate only the already reviewed MIT protocol header into project build/."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import os
ROOT=Path(__file__).resolve().parent.parent
XML=ROOT/'tests/input_lab/protocol/input-method-unstable-v2.xml'
SHA='99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703'
def main():
    if len(sys.argv)!=2:raise SystemExit('one build output required')
    output=Path(sys.argv[1]).resolve()
    if not output.is_relative_to((ROOT/'build').resolve()):raise SystemExit('project build output required')
    if hashlib.sha256(XML.read_bytes()).hexdigest()!=SHA:raise SystemExit('protocol pin mismatch')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.grab-header-',dir=output.parent)
    os.close(fd)
    try:
        subprocess.run(['/usr/bin/wayland-scanner','client-header',str(XML),name],check=True,env={'PATH':'/usr/bin'})
        os.replace(name,output)
    finally:
        Path(name).unlink(missing_ok=True)
if __name__=='__main__':main()
