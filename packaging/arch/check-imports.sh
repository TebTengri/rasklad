#!/bin/bash
# SPDX-License-Identifier: MIT
# SYS-258: the addon must not read the environment, load code, log, write files,
# or use D-Bus, the network or processes (libc names, then Qt classes, which nm
# shows mangled). Reading the pinned profile with QFile is expected; writing
# through Qt is not. Used by PKGBUILD check() and tools/ci.sh.
# usage: check-imports.sh <rasklad.so>
set -euo pipefail
imports=$(nm -D --undefined-only "$1")
[[ -n $imports ]] || { echo 'no imports read from the addon' >&2; exit 1; }
if grep -E -w 'getenv|secure_getenv|socket|connect|bind|send|sendto|sendmsg|getaddrinfo|fork|vfork|clone|exec[lv]p?e?|execveat|fexecve|posix_spawnp?|popen|system|syscall|ptrace|dlopen|dlsym|dbus_[a-z_]+|open|open64|openat|openat64|creat|fopen|fopen64|write|fwrite|syslog' <<<"$imports" ||
   grep -E 'QProcess|QDBus|QNetwork|QLocalSocket|QTcpSocket|QUdpSocket|QSaveFile|QTemporaryFile|QMessageLogger|QDebug|QLibrary|QPluginLoader|QSettings|_ZN9QIODevice5write|_ZN9QIODevice7putChar|_ZNK?4QDir|_ZN5QFile(6rename|6remove|4copy|4link|6resize)' <<<"$imports"; then
  echo 'unexpected import in rasklad.so' >&2
  exit 1
fi
echo 'imports: ok'
