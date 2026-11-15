// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <input-method-v2-client-protocol.h>
#include <wayland-client-core.h>

// Test-owned objects, never libwayland internals. No Wayland connection exists.
struct GrabFakeProxy {
    const char *kind="zwp_input_method_keyboard_grab_v2";
    std::uint32_t version=1;
    int result=0;
    void (**received)(void)=nullptr;
    void *registeredData=nullptr;
    zwp_input_method_keyboard_grab_v2_listener installed{};
    unsigned registrations=0;
    unsigned versionQueries=0;
};
inline wl_proxy *asProxy(GrabFakeProxy &p) {return reinterpret_cast<wl_proxy*>(&p);}
inline zwp_input_method_keyboard_grab_v2 *asGrab(GrabFakeProxy &p) {return reinterpret_cast<zwp_input_method_keyboard_grab_v2*>(&p);}
inline void (**asImplementation(zwp_input_method_keyboard_grab_v2_listener &listener))(void) {
    return reinterpret_cast<void (**)(void)>(&listener);
}
