// SPDX-License-Identifier: MIT
// Unit negative controls for the developer witness environment gate only.
// Does not start Fcitx, D-Bus, Wayland or any host input connection.
#include "fcitxlabwitness.h"
#include <cassert>
#include <cstdlib>
int main() {
    using rasklad::fcitxlab::witness::detail::privateLabEnvironment;
    const auto reset=[] {
        setenv("HOME","/home/lab",1);
        setenv("XDG_RUNTIME_DIR","/run/lab",1);
        setenv("RASKLAD_NATIVE_WAYLAND_LAB","1",1);
        setenv("RASKLAD_ADDON_WITNESS_LAB","1",1);
        setenv("DBUS_SESSION_BUS_ADDRESS","unix:path=/run/lab/dbus-fixed",1);
        unsetenv("DISPLAY");unsetenv("WAYLAND_SOCKET");unsetenv("RASKLAD_PARENT_FD");
    };
    reset();assert(privateLabEnvironment());
    for(const auto *key:{"HOME","XDG_RUNTIME_DIR","RASKLAD_NATIVE_WAYLAND_LAB","RASKLAD_ADDON_WITNESS_LAB"}) {
        reset();unsetenv(key);assert(!privateLabEnvironment());
        reset();setenv(key,"wrong",1);assert(!privateLabEnvironment());
    }
    for(const auto *key:{"DISPLAY","WAYLAND_SOCKET","RASKLAD_PARENT_FD"}) {
        reset();setenv(key,"",1);assert(!privateLabEnvironment());
    }
    for(const auto *address:{"unix:path=/run/other/bus","tcp:host=localhost","unix:path=/run/lab/a;unix:path=/run/lab/b"}) {
        reset();setenv("DBUS_SESSION_BUS_ADDRESS",address,1);assert(!privateLabEnvironment());
    }
}
