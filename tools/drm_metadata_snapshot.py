#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Small PCI metadata snapshot; default performs no host reads or connections.

libdrm2.4.134 drmGetDeviceFromDevId(flags=0) needs device/drm, subsystem,
PCI_SLOT_NAME and four PCI IDs. Source: official release xf86drm.c at
https://dri.freedesktop.org/libdrm/libdrm-2.4.134.tar.xz . Aquamarine v0.15.1
Wayland.cpp consumes the result; Backend.cpp/allocator/GBM.cpp add requirements
this snapshot does not prove. No PCI config/resource/ROM or whole sysfs bind.

collect_metadata() is explicit, injectable for tests, and stats but never opens
device nodes. write_snapshot() creates a NEW private directory under project
build/. Callers must use the same UID and verify current device identity before
mounting. Only renderD128 may be dev-bound; the primary node is stat-only and
MUST use a NODEV read-only bind. This is not protection from malicious same-UID
software, nor permission to connect host Wayland or open GPUs. Not integrated
into the live runner. Non-PCI, ambiguous and changed topology fail closed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import sys

ROOT = Path(__file__).resolve().parent.parent
ATTRIBUTES = ('vendor', 'device', 'subsystem_vendor', 'subsystem_device')
NODE = re.compile(r'(?:card[0-9]{1,4}|renderD[0-9]{1,5})\Z')
PCI_SLOT = re.compile(r'[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-1][0-9a-fA-F]\.[0-7]\Z')


def bounded_text(path, limit):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        metadata = os.fstat(fd)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid not in (0, os.getuid()):
            raise ValueError('metadata_file')
        chunks = []
        total = 0
        while total <= limit:
            chunk = os.read(fd, limit + 1 - total)
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
        if total > limit:
            raise ValueError('metadata_bound')
        return b''.join(chunks).decode('ascii')
    finally:
        os.close(fd)


def pci_id(text):
    if not re.fullmatch(r'0[xX][0-9a-fA-F]{1,4}\n?', text):
        raise ValueError('pci_id')
    return f'0x{int(text, 16):04x}\n'


def collect_metadata(sys_root=Path('/sys'), dev_root=Path('/dev'), *, stat_node=os.stat):
    """Explicit read-only collection. stat_node injection substitutes char metadata in tests."""
    sys_root = Path(sys_root).absolute()
    dev_root = Path(dev_root).absolute()
    devices = (sys_root / 'devices').resolve(strict=True)
    if devices != sys_root / 'devices':
        raise ValueError('sys_devices_redirect')
    node_dir = (sys_root / 'class/drm/renderD128').resolve(strict=True)
    device = (node_dir / 'device').resolve(strict=True)
    if not device.is_relative_to(devices) or device == devices:
        raise ValueError('pci_device_outside_devices')
    drm = (device / 'drm').resolve(strict=True)
    if drm != device / 'drm' or node_dir != drm / 'renderD128':
        raise ValueError('render_parent_mismatch')
    subsystem = device / 'subsystem'
    if not subsystem.is_symlink() or subsystem.resolve(strict=True) != sys_root / 'bus/pci':
        raise ValueError('non_pci_subsystem')
    attrs = {name: pci_id(bounded_text(device / name, 64)) for name in ATTRIBUTES}
    values = [line.partition('=')[2] for line in bounded_text(device / 'uevent', 4096).splitlines()
              if line.partition('=')[0] == 'PCI_SLOT_NAME']
    if len(values) != 1 or not PCI_SLOT.fullmatch(values[0]):
        raise ValueError('pci_slot')
    if (dev_root / 'dri').is_symlink():
        raise ValueError('device_directory_redirect')
    entries = list(drm.iterdir())
    if len(entries) > 16:
        raise ValueError('drm_entries_bound')
    nodes = []
    for entry in sorted(entries):
        if not NODE.fullmatch(entry.name):
            continue
        resolved = entry.resolve(strict=True)
        if resolved != drm / entry.name or (resolved / 'device').resolve(strict=True) != device:
            raise ValueError('sibling_parent_mismatch')
        text = bounded_text(resolved / 'dev', 32)
        if not re.fullmatch(r'226:[0-9]{1,5}\n?', text):
            raise ValueError('device_number')
        major, minor = map(int, text.strip().split(':'))
        path = dev_root / 'dri' / entry.name
        if path.is_symlink():
            raise ValueError('device_node_redirect')
        metadata = stat_node(path)
        if not stat.S_ISCHR(metadata.st_mode) or metadata.st_rdev != os.makedev(major, minor):
            raise ValueError('device_identity')
        if metadata.st_uid not in (0, os.getuid()):
            raise ValueError('device_owner')
        nodes.append({'name': entry.name, 'major': major, 'minor': minor,
                      'access': 'render' if entry.name == 'renderD128' else 'stat_only'})
    result = {'schema': 1, 'collectorUid': os.getuid(), 'pciSlot': values[0].lower(),
              'attributes': attrs, 'nodes': nodes}
    validate_metadata(result)
    return result


def validate_metadata(metadata):
    if metadata.get('schema') != 1 or metadata.get('collectorUid') != os.getuid():
        raise ValueError('same_uid_required')
    if not isinstance(metadata.get('pciSlot'), str) or not PCI_SLOT.fullmatch(metadata['pciSlot']):
        raise ValueError('pci_slot')
    attrs = metadata.get('attributes', {})
    if set(attrs) != set(ATTRIBUTES) or any(pci_id(attrs[name]) != attrs[name] for name in ATTRIBUTES):
        raise ValueError('pci_attributes')
    nodes = metadata.get('nodes', [])
    if not isinstance(nodes, list) or len(nodes) != 2:
        raise ValueError('single_gpu_topology_required')
    render = [n for n in nodes if n.get('name') == 'renderD128']
    primary = [n for n in nodes if re.fullmatch(r'card[0-9]{1,4}', n.get('name', ''))]
    if len(render) != 1 or len(primary) != 1:
        raise ValueError('single_gpu_topology_required')
    for node in nodes:
        if type(node.get('major')) is not int or node['major'] != 226 or type(node.get('minor')) is not int:
            raise ValueError('device_number')
        if not 0 <= node['minor'] <= 65535:
            raise ValueError('device_number')
        expected_access = 'render' if node['name'] == 'renderD128' else 'stat_only'
        if node.get('access') != expected_access:
            raise ValueError('node_access')
    if render[0]['minor'] != 128 or primary[0]['minor'] != int(primary[0]['name'][4:]):
        raise ValueError('node_minor_mismatch')
    if primary[0]['minor'] >= 64:
        raise ValueError('primary_minor_range')


def write_snapshot(metadata, directory, *, project_root=ROOT):
    """Create once under project build, with no host symlinks/tree copied into it."""
    validate_metadata(metadata)
    if '..' in Path(project_root).parts or '..' in Path(directory).parts:
        raise ValueError('project_build_only')
    project_root = Path(project_root).absolute()
    directory = Path(directory).absolute()
    build = project_root / 'build'
    if not directory.is_relative_to(build) or directory == build:
        raise ValueError('project_build_only')
    for ancestor in (project_root, *project_root.parents):
        if ancestor.is_symlink():
            raise ValueError('project_root_redirect')
    parent = directory.parent
    if not parent.is_dir():
        raise ValueError('existing_private_parent_required')
    for ancestor in (parent, *parent.parents):
        if ancestor == project_root.parent:
            break
        info = ancestor.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid not in (0, os.getuid()) or info.st_mode & 0o022:
            raise ValueError('snapshot_parent')
    if parent.stat().st_uid != os.getuid() or parent.stat().st_mode & 0o077:
        raise ValueError('existing_private_parent_required')
    directory.mkdir(mode=0o700)
    sysfs = directory / 'sys'
    pci = sysfs / 'devices/rasklad-pci'
    (pci / 'drm').mkdir(parents=True, mode=0o700)
    (sysfs / 'bus/pci').mkdir(parents=True, mode=0o700)
    (sysfs / 'dev/char').mkdir(parents=True, mode=0o700)
    (sysfs / 'class/drm').mkdir(parents=True, mode=0o700)
    (pci / 'subsystem').symlink_to('/sys/bus/pci')
    (pci / 'uevent').write_text('PCI_SLOT_NAME=' + metadata['pciSlot'] + '\n')
    for name, value in metadata['attributes'].items():
        (pci / name).write_text(value)
    for node in metadata['nodes']:
        name = node['name']
        target = pci / 'drm' / name
        target.mkdir(mode=0o700)
        (target / 'device').symlink_to('../..')
        (target / 'dev').write_text(f"{node['major']}:{node['minor']}\n")
        (sysfs / 'dev/char' / f"{node['major']}:{node['minor']}").symlink_to('../../devices/rasklad-pci/drm/' + name)
        (sysfs / 'class/drm' / name).symlink_to('../../devices/rasklad-pci/drm/' + name)
    return {'sysfs': str(sysfs), 'renderDevice': '/dev/dri/renderD128',
            'statOnlyPrimary': '/dev/dri/' + next(n['name'] for n in metadata['nodes'] if n['access'] == 'stat_only'),
            'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False,
            'limitations': 'metadata discovery only; GBM/Mesa requirements and same-UID races not solved'}


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    print(json.dumps({'schema': 1, 'status': 'plan_only', 'hostMetadataRead': False,
                      'devicesOpened': False, 'hostDisplayConnected': False,
                      'scope': 'one PCI renderD128 GPU; five bounded metadata values; primary NODEV stat-only'}, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
