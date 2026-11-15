# SPDX-License-Identifier: MIT
# Codex integration glue: separate developer-only addon variant which bridges
# the trusted preloaded Core-entry shim to the existing read-only lab witness.
# Build in a separate directory; never install or set a host LD_PRELOAD value.
# Ordinary and schema-2 witness artifacts are unaffected.

include(fcitx_witness_lab.pro)

DEFINES += RASKLAD_CORE_ENTRY_WITNESS_LAB
LIBS += -ldl
