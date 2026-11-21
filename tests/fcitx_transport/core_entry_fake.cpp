// SPDX-License-Identifier: MIT
// Fake original for an isolated ABI/forwarding unit test, never a lab runtime.
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
namespace { unsigned calls=0;bool entryOK=true; }
extern "C" __attribute__((visibility("default")))
bool fake_original(fcitx::InputContext*,fcitx::KeyEvent&) asm("_ZN5fcitx12InputContext8keyEventERNS_8KeyEventE");
extern "C" bool fake_original(fcitx::InputContext*,fcitx::KeyEvent &event) {
    using Snapshot=std::uint32_t(*)(std::uint32_t*,std::uint32_t) noexcept;
    void *raw=dlsym(RTLD_DEFAULT,"rasklad_core_entry_snapshot_v1");
    Snapshot snapshot=nullptr;static_assert(sizeof(snapshot)==sizeof(raw));
    std::memcpy(&snapshot,&raw,sizeof(snapshot));
    std::uint32_t out[12]{};
    const bool release=event.isRelease();
    if(!snapshot||snapshot(out,12)!=12||out[2]!=1||out[3]!=(release?1u:0u)
       ||out[4]!=1||out[5]!=(release?1u:0u))entryOK=false;
    ++calls;
    return release; // Known false press / true release must survive unchanged.
}
extern "C" __attribute__((visibility("default")))
std::uint32_t rasklad_fake_core_ok() {return calls==2&&entryOK?1u:0u;}
