// SPDX-License-Identifier: MIT
// C API interception only: does not start Fcitx or test real wire delivery.
#include "zwp_input_method_keyboard_grab_v2.h"
#include <wayland-client.h>
#include <cstdio>
#include <cstdint>

namespace {
struct Calls { unsigned release=0, destroy=0, flags=0; void *userdata=nullptr;
 void *listenerData=nullptr; void (**listener)(void)=nullptr; } calls;
int errors=0;
void check(bool condition){if(!condition)++errors;}
// Never passed to an actual Wayland function or dereferenced.
alignas(void*) unsigned char token;
}
extern "C" {
uint32_t __wrap_wl_proxy_get_version(wl_proxy *p){check(p==reinterpret_cast<wl_proxy*>(&token));return 1;}
void __wrap_wl_proxy_set_user_data(wl_proxy *p,void *data){check(p==reinterpret_cast<wl_proxy*>(&token));calls.userdata=data;}
int __wrap_wl_proxy_add_listener(wl_proxy *p,void (**listener)(void),void *data){
 check(p==reinterpret_cast<wl_proxy*>(&token));calls.listener=listener;calls.listenerData=data;return 0;
}
wl_proxy *__wrap_wl_proxy_marshal_flags(wl_proxy *p,uint32_t opcode,
 const wl_interface *interface,uint32_t version,uint32_t flags,...){
 check(p==reinterpret_cast<wl_proxy*>(&token));check(opcode==ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_RELEASE);
 check(interface==nullptr && version==1);
 ++calls.release;calls.flags=flags;return nullptr;
}
void __wrap_wl_proxy_destroy(wl_proxy *p){check(p==reinterpret_cast<wl_proxy*>(&token));++calls.destroy;}
}
int main(){
 using Grab=fcitx::wayland::ZwpInputMethodKeyboardGrabV2;
 auto *proxy=reinterpret_cast<zwp_input_method_keyboard_grab_v2*>(&token);
 {
  Grab grab(proxy);
  check(calls.userdata==&grab && calls.listenerData==&grab && calls.listener);
 }
 check(calls.release==1 && calls.flags==WL_MARSHAL_FLAG_DESTROY && calls.destroy==0);
 calls={};
 {
  Grab grab(proxy);
  check(grab.releaseForDrain());
  check(!grab.releaseForDrain());
  check(calls.release==1 && calls.flags==0 && calls.destroy==0);
  check(calls.userdata==&grab && calls.listenerData==&grab);
  // Queued listener dispatch must still target the living wrapper.
  unsigned keys=0;
  grab.key().connect([&](uint32_t,uint32_t,uint32_t key,uint32_t state){
   check(key==57 && state==1);++keys;
  });
  auto *listener=reinterpret_cast<const zwp_input_method_keyboard_grab_v2_listener*>(calls.listener);
  listener->key(calls.listenerData,proxy,1,10,57,1);
  check(keys==1);
 }
 check(calls.release==1 && calls.destroy==1);
 std::printf("{\"ownershipControlsPassed\":%s,\"realWire\":false,\"fcitxStarted\":false}\n",errors?"false":"true");
 return errors?1:0;
}
