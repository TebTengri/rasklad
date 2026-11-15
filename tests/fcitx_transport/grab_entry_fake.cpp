// SPDX-License-Identifier: MIT
// RTLD_NEXT originals for synthetic proxy objects only; no display or socket.
#include "grab_test_api.h"
#include <cstring>
#define EXPORTED extern "C" __attribute__((visibility("default")))
EXPORTED const char *wl_proxy_get_class(wl_proxy *proxy) {
    return proxy?reinterpret_cast<GrabFakeProxy*>(proxy)->kind:nullptr;
}
EXPORTED std::uint32_t wl_proxy_get_version(wl_proxy *proxy) {
    if(!proxy)return 0;
    auto &p=*reinterpret_cast<GrabFakeProxy*>(proxy);++p.versionQueries;return p.version;
}
EXPORTED int wl_proxy_add_listener(wl_proxy *proxy,void (**implementation)(void),void *data) {
    if(!proxy)return -9;
    auto &p=*reinterpret_cast<GrabFakeProxy*>(proxy);
    ++p.registrations;p.received=implementation;p.registeredData=data;
    if(p.result==0 && implementation)std::memcpy(&p.installed,implementation,sizeof(p.installed));
    return p.result;
}
