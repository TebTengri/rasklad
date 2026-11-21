// SPDX-License-Identifier: MIT
#pragma once
#include <wayland-server-core.h>

// No real Wayland object or connection is created by this closed fixture.
struct ServerRouteFake {
    unsigned originalCalls=0,loggerCalls=0,classQueries=0;
    bool nullOriginal=false,failLogger=false;
    wl_protocol_logger_func_t logger=nullptr;
    void *loggerData=nullptr;
    wl_display *lastDisplay=nullptr;
    unsigned char displayTokens[4]{};
    unsigned char loggerToken=0;
};
struct ServerRouteResource {
    const char *className;
    wl_client *client=reinterpret_cast<wl_client*>(0x1230);
    wl_list listeners{};
    bool initialized=false;
};
extern "C" void rasklad_server_fake_destroy(wl_resource *resource);
inline wl_resource *asResource(ServerRouteResource &resource){return reinterpret_cast<wl_resource*>(&resource);}
