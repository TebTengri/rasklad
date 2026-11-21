// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
struct WireClientResult {std::uint32_t press=0,release=0,protocolError=0,complete=0,failed=0;};
int wire_client(int socketFd,bool drain,WireClientResult &result);
