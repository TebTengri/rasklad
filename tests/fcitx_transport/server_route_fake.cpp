// SPDX-License-Identifier: MIT
// RTLD_NEXT test originals. Never creates display/socket/client/device state.
#include "server_route_fake.h"
namespace {ServerRouteFake state;}
#define EXPORTED extern "C" __attribute__((visibility("default")))
EXPORTED ServerRouteFake *rasklad_server_fake_v1(){return &state;}
EXPORTED wl_display *wl_display_create(){
    ++state.originalCalls;
    if(state.nullOriginal)return nullptr;
    auto *display=reinterpret_cast<wl_display*>(&state.displayTokens[(state.originalCalls-1)%4]);
    state.lastDisplay=display;return display;
}
EXPORTED wl_protocol_logger *wl_display_add_protocol_logger(wl_display *display,wl_protocol_logger_func_t callback,void *data){
    ++state.loggerCalls;
    if(state.failLogger)return nullptr;
    state.lastDisplay=display;state.logger=callback;state.loggerData=data;
    return reinterpret_cast<wl_protocol_logger*>(&state.loggerToken);
}
EXPORTED const char *wl_resource_get_class(const wl_resource *resource){
    ++state.classQueries;
    return resource?reinterpret_cast<const ServerRouteResource*>(resource)->className:nullptr;
}
