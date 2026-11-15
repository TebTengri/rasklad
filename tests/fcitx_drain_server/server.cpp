// SPDX-License-Identifier: MIT
// Closed synthetic protocol peer; never accepts or records arbitrary keys/text.
#include <wayland-server.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include "im-server.h"
#include "vk-server.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
struct Lab { wl_display *display; wl_resource *im=nullptr,*grab=nullptr,*vk=nullptr; int keymap=-1; uint32_t size=0; unsigned grabs=0, released=0, press=0, release=0, commits=0, doneCount=0, acknowledged=0; bool gated=false, flushed=false, waitedForAck=false; wl_event_source *settle=nullptr; bool vkReady=false, raced=false, failed=false, cleaning=false; } lab;
static void fail(){lab.failed=true;wl_display_terminate(lab.display);}
static void resourceGone(wl_resource*r){
 if(r==lab.grab)lab.grab=nullptr;
 if(r==lab.im)lab.im=nullptr;
 if(r==lab.vk)lab.vk=nullptr;
 if(!lab.cleaning)fail();
}
static void grabGone(wl_resource*r){if(r==lab.grab){lab.grab=nullptr;if(!lab.cleaning)fail();}}
static void destroy(wl_client*,wl_resource*r){wl_resource_destroy(r);}
static void done(){++lab.doneCount;zwp_input_method_v2_send_done(lab.im);}
static void maybeFlush(){
 if(!lab.gated || !lab.raced || lab.flushed || lab.grabs!=2 || lab.released!=1 ||
    !lab.grab || !lab.vkReady)return;
 if(lab.acknowledged!=lab.doneCount){lab.waitedForAck=true;return;}
 lab.flushed=true;
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,3,100,57,1);
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);
 wl_display_flush_clients(lab.display);
}
static void maybeRace(){if(lab.raced || !lab.vkReady || lab.grabs!=1 || !lab.commits || !lab.im || !lab.grab || !lab.vk)return;lab.raced=true;zwp_input_method_v2_send_deactivate(lab.im);done();zwp_input_method_v2_send_activate(lab.im);done();if(!lab.gated)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,3,100,57,1);wl_display_flush_clients(lab.display);}
static void releaseGrab(wl_client*c,wl_resource*r){if(r!=lab.grab){fail();return;}lab.grab=nullptr;++lab.released;destroy(c,r);}
static const struct zwp_input_method_keyboard_grab_v2_interface grabImpl={releaseGrab};
static void grab(wl_client*c,wl_resource*,uint32_t id){if(lab.grab || lab.grabs>=2){fail();return;}lab.grab=wl_resource_create(c,&zwp_input_method_keyboard_grab_v2_interface,1,id);wl_resource_set_implementation(lab.grab,&grabImpl,nullptr,grabGone);++lab.grabs;zwp_input_method_keyboard_grab_v2_send_keymap(lab.grab,1,lab.keymap,lab.size);zwp_input_method_keyboard_grab_v2_send_repeat_info(lab.grab,0,600);zwp_input_method_keyboard_grab_v2_send_modifiers(lab.grab,2,0,0,0,0);if(lab.grabs==2){if(lab.released!=1){fail();return;}if(!lab.gated)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);}maybeRace();maybeFlush();}
static void text(wl_client*,wl_resource*,const char*){fail();}
static void preedit(wl_client*,wl_resource*,const char*s,int32_t,int32_t){if(s && *s)fail();}
static void erase(wl_client*,wl_resource*,uint32_t,uint32_t){fail();}
static void commit(wl_client*,wl_resource*,uint32_t serial){++lab.commits;if(lab.commits>16){fail();return;}if(serial==lab.doneCount)lab.acknowledged=serial;maybeRace();maybeFlush();}
static void popup(wl_client*,wl_resource*,uint32_t,wl_resource*){fail();}
static const struct zwp_input_method_v2_interface imImpl={text,preedit,erase,commit,popup,grab,destroy};
static void getIM(wl_client*c,wl_resource*,wl_resource*,uint32_t id){if(lab.im){fail();return;}lab.im=wl_resource_create(c,&zwp_input_method_v2_interface,1,id);wl_resource_set_implementation(lab.im,&imImpl,nullptr,resourceGone);zwp_input_method_v2_send_activate(lab.im);zwp_input_method_v2_send_content_type(lab.im,0,0);done();}
static const struct zwp_input_method_manager_v2_interface imManager={getIM,destroy};
static void vkMap(wl_client*,wl_resource*,uint32_t format,int32_t fd,uint32_t size){close(fd);if(format!=1 || !size || size>1048576){fail();return;}lab.vkReady=true;maybeRace();}
static int settled(void*){wl_display_terminate(lab.display);return 0;}
static void vkKey(wl_client*,wl_resource*r,uint32_t,uint32_t key,uint32_t state){if((lab.gated && !lab.flushed) || !lab.raced || r!=lab.vk || key!=57 || state>1 || (state==1 && lab.release)){fail();return;}if(state)++lab.press;else ++lab.release;if(lab.press>1 || lab.release>1){fail();return;}if(lab.release)wl_event_source_timer_update(lab.settle,100);}
static void vkMods(wl_client*,wl_resource*,uint32_t,uint32_t,uint32_t,uint32_t){}
static const struct zwp_virtual_keyboard_v1_interface vkImpl={vkMap,vkKey,vkMods,destroy};
static void createVK(wl_client*c,wl_resource*,wl_resource*,uint32_t id){if(lab.vk){fail();return;}auto*r=wl_resource_create(c,&zwp_virtual_keyboard_v1_interface,1,id);lab.vk=r;wl_resource_set_implementation(r,&vkImpl,nullptr,resourceGone);}
static const struct zwp_virtual_keyboard_manager_v1_interface vkManager={createVK};
static void keyboard(wl_client*c,wl_resource*r,uint32_t id){auto*k=wl_resource_create(c,&wl_keyboard_interface,std::min(7,wl_resource_get_version(r)),id);static const struct wl_keyboard_interface impl={destroy};wl_resource_set_implementation(k,&impl,nullptr,nullptr);wl_keyboard_send_keymap(k,1,lab.keymap,lab.size);if(wl_resource_get_version(k)>=4)wl_keyboard_send_repeat_info(k,0,600);}
static void unsupported(wl_client*,wl_resource*,uint32_t){fail();}
static const struct wl_seat_interface seatImpl={unsupported,keyboard,unsupported,destroy};
static void bindSeat(wl_client*c,void*,uint32_t v,uint32_t id){auto*r=wl_resource_create(c,&wl_seat_interface,std::min(v,7u),id);wl_resource_set_implementation(r,&seatImpl,nullptr,nullptr);wl_seat_send_capabilities(r,WL_SEAT_CAPABILITY_KEYBOARD);if(v>=2)wl_seat_send_name(r,"lab-seat");}
static void bindIM(wl_client*c,void*,uint32_t,uint32_t id){auto*r=wl_resource_create(c,&zwp_input_method_manager_v2_interface,1,id);wl_resource_set_implementation(r,&imManager,nullptr,nullptr);}
static void bindVK(wl_client*c,void*,uint32_t,uint32_t id){auto*r=wl_resource_create(c,&zwp_virtual_keyboard_manager_v1_interface,1,id);wl_resource_set_implementation(r,&vkManager,nullptr,nullptr);}
static int deadline(void*){fail();return 0;}
int main(int argc,char **argv){if(argc==2 && !strcmp(argv[1],"ack-gated"))lab.gated=true;else if(argc!=1)return 2;if(!getenv("RASKLAD_NATIVE_WAYLAND_LAB") || strcmp(getenv("XDG_RUNTIME_DIR")?getenv("XDG_RUNTIME_DIR"):"","/run/lab"))return 2;auto*ctx=xkb_context_new(XKB_CONTEXT_NO_FLAGS);xkb_rule_names names{};names.layout="us";auto*map=xkb_keymap_new_from_names(ctx,&names,XKB_KEYMAP_COMPILE_NO_FLAGS);if(!map)return 2;char*s=xkb_keymap_get_as_string(map,XKB_KEYMAP_FORMAT_TEXT_V1);lab.size=strlen(s)+1;lab.keymap=memfd_create("lab-keymap",MFD_CLOEXEC);if(lab.keymap<0 || write(lab.keymap,s,lab.size)!=lab.size)return 2;free(s);xkb_keymap_unref(map);xkb_context_unref(ctx);lab.display=wl_display_create();if(!lab.display)return 2;wl_global_create(lab.display,&wl_seat_interface,7,nullptr,bindSeat);wl_global_create(lab.display,&zwp_input_method_manager_v2_interface,1,nullptr,bindIM);wl_global_create(lab.display,&zwp_virtual_keyboard_manager_v1_interface,1,nullptr,bindVK);if(wl_display_add_socket(lab.display,"wayland-lab"))return 2;lab.settle=wl_event_loop_add_timer(wl_display_get_event_loop(lab.display),settled,nullptr);if(!lab.settle)return 2;auto*t=wl_event_loop_add_timer(wl_display_get_event_loop(lab.display),deadline,nullptr);wl_event_source_timer_update(t,5000);puts("{\"ready\":true}");fflush(stdout);wl_display_run(lab.display);bool ok=!lab.failed && lab.raced && lab.grabs==2 && lab.released==1 && lab.release==1 && (!lab.gated || (lab.flushed && lab.acknowledged==lab.doneCount && lab.waitedForAck));printf("{\"completed\":%s,\"vkPress\":%u,\"vkRelease\":%u,\"oneLiveGrabInvariant\":%s,\"grabs\":%u,\"commits\":%u,\"vkReady\":%s,\"raced\":%s,\"ackMatched\":%s,\"gatedPairSent\":%s,\"waitedForAck\":%s}\n",ok?"true":"false",lab.press,lab.release,lab.failed?"false":"true",lab.grabs,lab.commits,lab.vkReady?"true":"false",lab.raced?"true":"false",lab.acknowledged==lab.doneCount?"true":"false",lab.flushed?"true":"false",lab.waitedForAck?"true":"false");fflush(stdout);if(ok && getchar()!='\n')ok=false;lab.cleaning=true;wl_display_destroy_clients(lab.display);wl_display_destroy(lab.display);close(lab.keymap);return ok?0:2;}
