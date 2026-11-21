#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assemble and verify reviewed, offline Hunspell language packs for rasklad.

The caller-supplied (catalog) record is the only trust anchor. Pack contents are
opaque bytes: this module never parses dictionaries, runs engines, downloads,
extracts archives, infers license compatibility or grants permissions.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import stat
import sys
from pathlib import Path, PurePosixPath

MIB = 1024 * 1024
FORMAT = "hunspell-utf8-v1"
ENGINE = "hunspell-1.7.5"
MANIFEST = "manifest.json"
CATALOG_MAX = 256 * 1024
CATALOG_MAX_PACKS = 256
ROLE_LIMITS = {"aff": MIB, "dic": 16 * MIB, "notice": MIB, "modifications": MIB}
TOTAL_MAX = 24 * MIB
MIN_FILES, MAX_FILES, MAX_DEPTH, MAX_PATH, MAX_DIRS = 4, 16, 4, 128, 48
MAX_ENTRIES = MAX_FILES + 1 + MAX_DIRS
PURPOSES = {"local-test": "local_test", "distribution": "distribution"}
RECORD_KEYS = {"id", "locale", "revision", "format", "engine", "provenance", "admission", "files"}
PROVENANCE_KEYS = {"repository", "commit", "review"}
ADMISSION_KEYS = {"local_test", "distribution"}
FILE_KEYS = {"role", "path", "sha256", "bytes"}
RENAME_NOREPLACE = 1
CHUNK = MIB

ID_RE = re.compile(r"[a-z0-9][a-z0-9-]{0,63}")
LOCALE_RE = re.compile(r"[a-z]{2,3}(?:-[A-Za-z0-9]{2,8}){0,3}")
HEX40_RE = re.compile(r"[0-9a-f]{40}")
HEX64_RE = re.compile(r"[0-9a-f]{64}")
SEG_RE = re.compile(r"[A-Za-z0-9_.-]+")
_LABEL = r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
REPO_RE = re.compile(rf"https://{_LABEL}(?:\.{_LABEL})+(?::[0-9]{{1,5}})?(?:/[A-Za-z0-9._~-]+)+/?")

DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
READ_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
WRITE_FLAGS = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC


class PackError(Exception):
    """Fail-closed error carrying only a generic, data-free code."""

    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


def _req(condition: object, code: str = "invalid-record") -> None:
    if not condition:
        raise PackError(code)


def _text(value: object, limit: int) -> bool:
    return type(value) is str and 0 < len(value) <= limit


def _exact(obj: object, keys: set, code: str = "invalid-record") -> None:
    _req(type(obj) is dict and obj.keys() == keys, code)


def _check_path(path: object) -> list:
    _req(_text(path, MAX_PATH))
    segments = path.split("/")
    _req(1 <= len(segments) <= MAX_DEPTH)
    for seg in segments:
        _req(SEG_RE.fullmatch(seg) is not None and seg not in (".", ".."))
    return segments


def validate_record(record: object) -> tuple:
    """Validate a reviewer-owned record; return ([(role, path, sha, size)], dirs)."""
    _exact(record, RECORD_KEYS)
    _req(_text(record["id"], 64) and ID_RE.fullmatch(record["id"]))
    _req(_text(record["locale"], 35) and LOCALE_RE.fullmatch(record["locale"]))
    _req(_text(record["revision"], 128) and record["revision"].isprintable())
    _req(type(record["format"]) is str and record["format"] == FORMAT)
    _req(type(record["engine"]) is str and record["engine"] == ENGINE)
    prov = record["provenance"]
    _exact(prov, PROVENANCE_KEYS)
    _req(_text(prov["repository"], 256) and REPO_RE.fullmatch(prov["repository"]))
    _req(_text(prov["commit"], 40) and HEX40_RE.fullmatch(prov["commit"]))
    _check_path(prov["review"])
    admission = record["admission"]
    _exact(admission, ADMISSION_KEYS)
    _req(all(type(admission[key]) is bool for key in ADMISSION_KEYS))
    files = record["files"]
    _req(type(files) is list and MIN_FILES <= len(files) <= MAX_FILES)
    entries, roles, folded, dirs, total = [], {}, set(), {}, 0
    for item in files:
        _exact(item, FILE_KEYS)
        role, path, sha, size = item["role"], item["path"], item["sha256"], item["bytes"]
        _req(type(role) is str and role in ROLE_LIMITS)
        segments = _check_path(path)
        _req(type(sha) is str and HEX64_RE.fullmatch(sha))
        _req(type(size) is int and 0 < size <= ROLE_LIMITS[role])
        _req(role != "aff" or path.endswith(".aff"))
        _req(role != "dic" or path.endswith(".dic"))
        key = path.lower()
        _req(key != MANIFEST and key not in folded)
        folded.add(key)
        for depth in range(1, len(segments)):
            parent = "/".join(segments[:depth])
            _req(dirs.setdefault(parent.lower(), parent) == parent)
        roles[role] = roles.get(role, 0) + 1
        total += size
        entries.append((role, path, sha, size))
    _req(total <= TOTAL_MAX and len(dirs) <= MAX_DIRS)
    _req(roles.get("aff") == 1 and roles.get("dic") == 1 and roles.get("notice", 0) >= 1
         and roles.get("modifications") == 1)
    _req(not folded & dirs.keys() and MANIFEST not in dirs)
    return entries, set(dirs.values())


def canonical_manifest(record: dict) -> bytes:
    text = json.dumps(record, sort_keys=True, ensure_ascii=True, indent=2) + "\n"
    return text.encode("ascii")


def _trusted(record: object, purpose: object) -> tuple:
    """Validate, snapshot (immune to later caller mutation) and admit a record."""
    _req(type(purpose) is str and purpose in PURPOSES, "invalid-purpose")
    validate_record(record)
    try:
        manifest = canonical_manifest(record)
    except (TypeError, ValueError):
        raise PackError("invalid-record") from None
    snapshot = json.loads(manifest.decode("ascii"))
    files, dirs = validate_record(snapshot)
    _req(canonical_manifest(snapshot) == manifest)
    # Each purpose needs its own explicit grant; distribution never implies local-test.
    _req(snapshot["admission"][PURPOSES[purpose]] is True, "not-admitted")
    return snapshot, files, dirs, manifest


def _open_dir(dir_fd: int, name: str) -> int:
    fd = os.open(name, DIR_FLAGS, dir_fd=dir_fd)
    if not stat.S_ISDIR(os.fstat(fd).st_mode):
        os.close(fd)
        raise PackError("not-directory")
    return fd


def _open_rel_dir(root_fd: int, segments: list) -> int:
    fd = os.dup(root_fd)
    try:
        for seg in segments:
            next_fd = _open_dir(fd, seg)
            os.close(fd)
            fd = next_fd
    except BaseException:
        os.close(fd)
        raise
    return fd


def _abs_parts(path: object) -> list:
    _req(isinstance(path, (str, os.PathLike)), "invalid-path")
    text = os.fspath(path)
    _req(isinstance(text, str) and text and "\x00" not in text, "invalid-path")
    _req(".." not in PurePosixPath(text).parts, "invalid-path")
    return [part for part in os.path.abspath(text).split("/") if part]


def _open_abs(parts: list) -> int:
    """Walk from '/' without following any symlink in any component."""
    root = os.open("/", DIR_FLAGS)
    try:
        return _open_rel_dir(root, parts)
    finally:
        os.close(root)


def _open_parent(path: object) -> tuple:
    parts = _abs_parts(path)
    _req(parts, "invalid-path")
    return _open_abs(parts[:-1]), parts[-1]


def _read_regular(dir_fd: int, name: str, limit: int, exact: int | None = None,
                  no_exec: bool = False) -> bytes:
    fd = os.open(name, READ_FLAGS, dir_fd=dir_fd)
    chunks, total = [], 0
    try:
        info = os.fstat(fd)
        _req(stat.S_ISREG(info.st_mode), "not-regular-file")
        _req(not (no_exec and info.st_mode & 0o111), "executable-file")
        _req(info.st_size <= limit and exact in (None, info.st_size), "size-mismatch")
        while total <= limit:
            block = os.read(fd, min(CHUNK, limit + 1 - total))
            if not block:
                break
            chunks.append(block)
            total += len(block)
    finally:
        os.close(fd)
    _req(total <= limit and exact in (None, total), "size-mismatch")
    return b"".join(chunks)


def _read_rel(root_fd: int, rel: str, size: int, sha: str | None, no_exec: bool = False) -> bytes:
    *parents, leaf = rel.split("/")
    dir_fd = _open_rel_dir(root_fd, parents)
    try:
        data = _read_regular(dir_fd, leaf, size, size, no_exec)
    finally:
        os.close(dir_fd)
    _req(sha is None or hashlib.sha256(data).hexdigest() == sha, "hash-mismatch")
    return data


class _Writer:
    """Tracks exactly what this invocation created so rollback never touches anything else."""

    def __init__(self) -> None:
        self.undo: list = []
        self.fds: list = []

    def mkdir(self, dir_fd: int, name: str) -> int:
        os.mkdir(name, 0o755, dir_fd=dir_fd)
        fd = _open_dir(dir_fd, name)
        self.fds.append(fd)
        info = os.fstat(fd)
        self.undo.append((dir_fd, name, info.st_dev, info.st_ino, True))
        return fd

    def write(self, dir_fd: int, name: str, data: bytes) -> None:
        fd = os.open(name, WRITE_FLAGS, 0o444, dir_fd=dir_fd)
        try:
            info = os.fstat(fd)
            self.undo.append((dir_fd, name, info.st_dev, info.st_ino, False))
            os.fchmod(fd, 0o444)
            view = memoryview(data)
            while view:
                view = view[os.write(fd, view):]
            os.fsync(fd)
        finally:
            os.close(fd)

    def rollback(self) -> None:
        for dir_fd, name, dev, ino, is_dir in reversed(self.undo):
            try:
                info = os.stat(name, dir_fd=dir_fd, follow_symlinks=False)
                if (info.st_dev, info.st_ino) == (dev, ino):
                    (os.rmdir if is_dir else os.unlink)(name, dir_fd=dir_fd)
            except OSError:
                pass  # Non-empty or replaced entries are left alone: no recursive deletion.

    def close(self) -> None:
        for fd in self.fds:
            os.close(fd)
        self.fds = []


def _populate(writer: _Writer, parent_fd: int, top: str, items: list) -> None:
    top_fd = writer.mkdir(parent_fd, top)
    dirs = {"": top_fd}
    for rel, data in items:
        *parents, leaf = rel.split("/")
        fd, key = top_fd, ""
        for seg in parents:
            key += seg + "/"
            if key not in dirs:
                dirs[key] = writer.mkdir(fd, seg)
            fd = dirs[key]
        writer.write(fd, leaf, data)
    for fd in dirs.values():
        os.fsync(fd)


def _renameat2():
    if not sys.platform.startswith("linux"):
        return None
    try:
        import ctypes
        func = ctypes.CDLL(None, use_errno=True).renameat2
    except (OSError, AttributeError):
        return None
    func.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    func.restype = ctypes.c_int

    def rename(dir_fd: int, old: str, new: str) -> None:
        if func(dir_fd, os.fsencode(old), dir_fd, os.fsencode(new), RENAME_NOREPLACE) != 0:
            raise OSError(ctypes.get_errno() or 5, "rename failed")

    return rename


def _publish(destination: object, items: list) -> None:
    parent_fd, name = _open_parent(destination)
    writer, done = _Writer(), False
    try:
        _req(len(name) <= MAX_PATH and SEG_RE.fullmatch(name) and name not in (".", ".."),
             "invalid-output")
        try:
            os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            raise PackError("output-exists")
        except FileNotFoundError:
            pass
        rename = _renameat2()
        if rename is None:  # Fallback: exclusive mkdir of the output itself, never overwrite.
            _populate(writer, parent_fd, name, items)
        else:
            temp = f".{name}.tmp-{secrets.token_hex(8)}"
            _populate(writer, parent_fd, temp, items)
            rename(parent_fd, temp, name)
        done = True
        os.fsync(parent_fd)
    finally:
        if not done:
            writer.rollback()
        writer.close()
        os.close(parent_fd)


def _summary(status: str, record: dict, purpose: str) -> dict:
    return {"status": status, "id": record["id"], "locale": record["locale"],
            "revision": record["revision"], "purpose": purpose, "files": len(record["files"])}


def build_pack(record: dict, source: Path, destination: Path, purpose: str = "local-test") -> dict:
    """Verify every source byte against the trusted record, then publish without clobbering."""
    try:
        snapshot, files, _dirs, manifest = _trusted(record, purpose)
        root = _open_abs(_abs_parts(source))
        try:
            items = [(path, _read_rel(root, path, size, sha)) for _role, path, sha, size in files]
        finally:
            os.close(root)
        items.append((MANIFEST, manifest))
        _publish(destination, items)
    except FileExistsError:
        raise PackError("output-exists") from None
    except OSError:
        raise PackError("io-error") from None
    return _summary("built", snapshot, purpose)


def _walk(fd: int, prefix: str, files: set, dirs: set, seen: tuple, count: list) -> None:
    with os.scandir(fd) as entries:
        for entry in entries:
            count[0] += 1
            _req(count[0] <= MAX_ENTRIES, "unexpected-entry")
            rel = prefix + entry.name
            mode = entry.stat(follow_symlinks=False).st_mode
            if stat.S_ISDIR(mode) and rel in dirs:
                seen[1].add(rel)
                sub = _open_dir(fd, entry.name)
                try:
                    _walk(sub, rel + "/", files, dirs, seen, count)
                finally:
                    os.close(sub)
            elif stat.S_ISREG(mode) and rel in files:
                seen[0].add(rel)
            else:
                raise PackError("unexpected-entry")


def verify_pack(record: dict, directory: Path, purpose: str = "local-test") -> dict:
    """Check a pack against the trusted record; the pack's own manifest grants nothing."""
    try:
        snapshot, files, dirs, manifest = _trusted(record, purpose)
        root = _open_abs(_abs_parts(directory))
        try:
            expected = {path for _role, path, _sha, _size in files} | {MANIFEST}
            seen = (set(), set())
            _walk(root, "", expected, dirs, seen, [0])
            _req(seen[0] == expected and seen[1] == dirs, "pack-mismatch")
            for _role, path, sha, size in files:
                _read_rel(root, path, size, sha, no_exec=True)
            stored = _read_rel(root, MANIFEST, len(manifest), None, no_exec=True)
            _req(stored == manifest, "manifest-mismatch")
        finally:
            os.close(root)
    except OSError:
        raise PackError("io-error") from None
    return _summary("verified", snapshot, purpose)


def _unique_pairs(pairs: list) -> dict:
    result = {}
    for key, value in pairs:
        _req(key not in result, "invalid-catalog")
        result[key] = value
    return result


def _reject_constant(_value: str) -> None:
    raise PackError("invalid-catalog")


def load_catalog(path: Path) -> list:
    parent_fd, name = _open_parent(path)
    try:
        raw = _read_regular(parent_fd, name, CATALOG_MAX)
    finally:
        os.close(parent_fd)
    try:
        catalog = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_pairs,
                             parse_constant=_reject_constant)
    except (UnicodeDecodeError, ValueError, RecursionError):
        raise PackError("invalid-catalog") from None
    _exact(catalog, {"schema", "packs"}, "invalid-catalog")
    _req(type(catalog["schema"]) is int and catalog["schema"] == 1, "invalid-catalog")
    packs = catalog["packs"]
    _req(type(packs) is list and 1 <= len(packs) <= CATALOG_MAX_PACKS, "invalid-catalog")
    ids = set()
    for record in packs:
        validate_record(record)
        _req(record["id"] not in ids, "invalid-catalog")
        ids.add(record["id"])
    return packs


def select_record(packs: list, pack_id: object) -> dict:
    _req(type(pack_id) is str and ID_RE.fullmatch(pack_id), "invalid-id")
    for record in packs:
        if record["id"] == pack_id:
            return record
    raise PackError("unknown-pack")


class _Parser(argparse.ArgumentParser):
    def error(self, message: str) -> None:  # noqa: ARG002 - never echo user input
        raise PackError("usage")


def main(argv: list | None = None) -> int:
    parser = _Parser(prog="language_pack", description="Build or verify a reviewed language pack.")
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--id", required=True)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify", type=Path)
    parser.add_argument("--purpose", choices=sorted(PURPOSES), default="local-test")
    try:
        args = parser.parse_args(argv)
        building = args.source is not None or args.output is not None
        _req(building != (args.verify is not None), "usage")
        _req(not building or (args.source is not None and args.output is not None), "usage")
        record = select_record(load_catalog(args.catalog), args.id)
        if building:
            result = build_pack(record, args.source, args.output, args.purpose)
        else:
            result = verify_pack(record, args.verify, args.purpose)
    except PackError as error:
        print(json.dumps({"error": error.code}), file=sys.stderr)
        return 1
    except OSError:
        print(json.dumps({"error": "io-error"}), file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
