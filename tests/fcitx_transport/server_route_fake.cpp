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

EXPORTED wl_client *wl_resource_get_client(wl_resource *resource){
    return resource?reinterpret_cast<ServerRouteResource*>(resource)->client:nullptr;
}
EXPORTED void wl_resource_add_destroy_listener(wl_resource *resource,wl_listener *listener){
    auto *fake=reinterpret_cast<ServerRouteResource*>(resource);
    if(!fake->initialized){wl_list_init(&fake->listeners);fake->initialized=true;}
    wl_list_insert(fake->listeners.prev,&listener->link);
}
EXPORTED void rasklad_server_fake_destroy(wl_resource *resource){
    auto *fake=reinterpret_cast<ServerRouteResource*>(resource);
    if(!fake->initialized)return;
    while(!wl_list_empty(&fake->listeners)){
        auto *listener=reinterpret_cast<wl_listener*>(fake->listeners.next);
        listener->notify(listener,resource);
    }
}
