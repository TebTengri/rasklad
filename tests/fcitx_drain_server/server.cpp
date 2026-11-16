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
struct Lab { wl_display *display; wl_resource *im=nullptr,*grab=nullptr,*vk=nullptr; int keymap=-1; uint32_t size=0; unsigned grabs=0, released=0, press=0, release=0, commits=0, doneCount=0, acknowledged=0; uint32_t shiftMask=0, vkModifierMask=0; bool modifier=false, modifierDown=false, modifierUp=false, shiftAtPress=false, shiftAtRelease=false, modifierWaitedForAck=false; bool gated=false, retained=false, ab=false, cross=false, crossActive=false, fresh=false, unknown=false, crossTransition=false, inactive=false, inactiveRaw=false, rawReleaseSent=false, releaseSent=false, flushed=false, waitedForAck=false; wl_event_source *settle=nullptr,*hold=nullptr; bool vkReady=false, raced=false, failed=false, cleaning=false; } lab;
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
static void maybeRelease(){
 if(!lab.ab || !lab.raced || lab.releaseSent || !lab.grab)return;
 if(lab.acknowledged!=lab.doneCount){if(lab.modifier)lab.modifierWaitedForAck=true;return;}
 lab.releaseSent=true;
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);
 if(lab.modifier)zwp_input_method_keyboard_grab_v2_send_modifiers(lab.grab,5,0,0,0,0);
 wl_display_flush_clients(lab.display);
}
static void maybeFlush(){
 if(!lab.gated || !lab.raced || lab.flushed || lab.grabs!=2 || lab.released!=1 ||
    !lab.grab || !lab.vkReady)return;
 if(lab.acknowledged!=lab.doneCount){lab.waitedForAck=true;return;}
 lab.flushed=true;
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,3,100,57,1);
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);
 wl_display_flush_clients(lab.display);
}
static void maybeRace(){if(lab.raced || !lab.vkReady || lab.grabs!=1 || !lab.commits || !lab.im || !lab.grab || !lab.vk)return;puts("{\"probeReady\":true}");fflush(stdout);if(getchar()!='\n'){fail();return;}lab.raced=true;if(lab.modifier)zwp_input_method_keyboard_grab_v2_send_modifiers(lab.grab,2,lab.shiftMask,0,0,0);if(lab.unknown){zwp_input_method_v2_send_deactivate(lab.im);done();zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);zwp_input_method_v2_send_activate(lab.im);done();wl_display_flush_clients(lab.display);return;}if(lab.cross){zwp_input_method_keyboard_grab_v2_send_key(lab.grab,3,100,57,1);wl_display_flush_clients(lab.display);return;}zwp_input_method_v2_send_deactivate(lab.im);done();if(!lab.inactive){zwp_input_method_v2_send_activate(lab.im);done();}if(!lab.gated)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,3,100,57,1);if(lab.retained && !lab.inactiveRaw && !lab.modifier)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);if(lab.modifier)maybeRelease();wl_display_flush_clients(lab.display);}
static void releaseGrab(wl_client*c,wl_resource*r){if(r!=lab.grab){fail();return;}lab.grab=nullptr;++lab.released;destroy(c,r);}
static const struct zwp_input_method_keyboard_grab_v2_interface grabImpl={releaseGrab};
static void grab(wl_client*c,wl_resource*,uint32_t id){if(lab.grab || lab.grabs>=2){fail();return;}lab.grab=wl_resource_create(c,&zwp_input_method_keyboard_grab_v2_interface,1,id);wl_resource_set_implementation(lab.grab,&grabImpl,nullptr,grabGone);++lab.grabs;zwp_input_method_keyboard_grab_v2_send_keymap(lab.grab,1,lab.keymap,lab.size);zwp_input_method_keyboard_grab_v2_send_repeat_info(lab.grab,lab.inactiveRaw?25:0,lab.inactiveRaw?200:600);zwp_input_method_keyboard_grab_v2_send_modifiers(lab.grab,2,0,0,0,0);if(lab.grabs==2){if(lab.released!=1){fail();return;}if(!lab.gated && !lab.ab && !lab.cross)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,101,57,0);}maybeRace();maybeFlush();maybeRelease();}
static void text(wl_client*,wl_resource*,const char*){fail();}
static void preedit(wl_client*,wl_resource*,const char*s,int32_t,int32_t){if(s && *s)fail();}
static void erase(wl_client*,wl_resource*,uint32_t,uint32_t){fail();}
static void commit(wl_client*,wl_resource*,uint32_t serial){++lab.commits;if(lab.commits>16){fail();return;}if(serial==lab.doneCount)lab.acknowledged=serial;maybeRace();maybeFlush();maybeRelease();}
static void popup(wl_client*,wl_resource*,uint32_t,wl_resource*){fail();}
static const struct zwp_input_method_v2_interface imImpl={text,preedit,erase,commit,popup,grab,destroy};
static void getIM(wl_client*c,wl_resource*,wl_resource*,uint32_t id){if(lab.im){fail();return;}lab.im=wl_resource_create(c,&zwp_input_method_v2_interface,1,id);wl_resource_set_implementation(lab.im,&imImpl,nullptr,resourceGone);zwp_input_method_v2_send_activate(lab.im);zwp_input_method_v2_send_content_type(lab.im,0,0);done();}
static const struct zwp_input_method_manager_v2_interface imManager={getIM,destroy};
static void vkMap(wl_client*,wl_resource*,uint32_t format,int32_t fd,uint32_t size){close(fd);if(format!=1 || !size || size>1048576){fail();return;}lab.vkReady=true;maybeRace();}
static int rawRelease(void*){
 if(!lab.inactiveRaw || !lab.grab || lab.rawReleaseSent){fail();return 0;}
 lab.rawReleaseSent=true;
 zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,450,57,0);
 zwp_input_method_v2_send_activate(lab.im);done();wl_display_flush_clients(lab.display);return 0;
}
static int settled(void*){wl_display_terminate(lab.display);return 0;}
static void vkKey(wl_client*,wl_resource*r,uint32_t,uint32_t key,uint32_t state){if((lab.gated && !lab.flushed) || !lab.raced || r!=lab.vk || key!=57 || state>1 || (state==1 && lab.release && !lab.fresh)){fail();return;}if(lab.fresh && state!=((lab.press+lab.release)%2==0?1u:0u)){fail();return;}if(lab.modifier){if(lab.vkModifierMask!=lab.shiftMask || !lab.modifierDown || lab.modifierUp || (state==0 && !lab.releaseSent)){fail();return;}if(state)lab.shiftAtPress=true;else lab.shiftAtRelease=true;}if(state)++lab.press;else ++lab.release;if(lab.press>(lab.fresh?2u:1u) || lab.release>(lab.fresh?2u:1u) || (lab.inactiveRaw && state==0 && !lab.rawReleaseSent)){fail();return;}if(lab.cross && state==1 && !lab.crossTransition){lab.crossTransition=true;zwp_input_method_v2_send_deactivate(lab.im);done();if(lab.crossActive){zwp_input_method_v2_send_activate(lab.im);done();}if(lab.fresh)zwp_input_method_keyboard_grab_v2_send_key(lab.grab,5,101,57,1);zwp_input_method_keyboard_grab_v2_send_key(lab.grab,4,102,57,0);if(!lab.crossActive){zwp_input_method_v2_send_activate(lab.im);done();}wl_display_flush_clients(lab.display);}if(lab.inactiveRaw && state==1)wl_event_source_timer_update(lab.hold,350);if(lab.release)wl_event_source_timer_update(lab.settle,100);}
static void vkMods(wl_client*,wl_resource*r,uint32_t depressed,uint32_t latched,uint32_t locked,uint32_t group){
 if(!lab.modifier)return;
 if(r!=lab.vk || latched || locked || group || (depressed!=0 && depressed!=lab.shiftMask)){fail();return;}
 lab.vkModifierMask=depressed;
 if(depressed==lab.shiftMask){if(!lab.raced || lab.modifierUp){fail();return;}lab.modifierDown=true;}
 else if(lab.modifierDown){if(!lab.releaseSent || lab.release!=1){fail();return;}lab.modifierUp=true;}
}
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
int main(int argc,char **argv){if(argc==2 && !strcmp(argv[1],"ack-gated"))lab.gated=true;else if(argc==2 && !strcmp(argv[1],"retained"))lab.retained=true;else if(argc==2 && !strcmp(argv[1],"grab-ab"))lab.ab=true;else if(argc==2 && !strcmp(argv[1],"modifier-retained")){lab.modifier=true;lab.ab=true;lab.retained=true;}else if(argc==2 && !strcmp(argv[1],"inactive"))lab.inactive=true;else if(argc==2 && !strcmp(argv[1],"inactive-raw")){lab.inactive=true;lab.inactiveRaw=true;lab.retained=true;}else if(argc==2 && !strcmp(argv[1],"cross-deactivate")){lab.cross=true;}else if(argc==2 && !strcmp(argv[1],"cross-active-release")){lab.cross=true;lab.crossActive=true;}else if(argc==2 && !strcmp(argv[1],"fresh-after-cancel")){lab.cross=true;lab.crossActive=true;lab.fresh=true;}else if(argc==2 && !strcmp(argv[1],"unknown-release")){lab.unknown=true;lab.retained=true;}else if(argc!=1)return 2;if(!getenv("RASKLAD_NATIVE_WAYLAND_LAB") || strcmp(getenv("XDG_RUNTIME_DIR")?getenv("XDG_RUNTIME_DIR"):"","/run/lab"))return 2;auto*ctx=xkb_context_new(XKB_CONTEXT_NO_FLAGS);xkb_rule_names names{};names.layout="us";auto*map=xkb_keymap_new_from_names(ctx,&names,XKB_KEYMAP_COMPILE_NO_FLAGS);if(!map)return 2;if(lab.modifier){auto shift=xkb_keymap_mod_get_index(map,XKB_MOD_NAME_SHIFT);if(shift==XKB_MOD_INVALID || shift>=32)return 2;lab.shiftMask=uint32_t{1}<<shift;}char*s=xkb_keymap_get_as_string(map,XKB_KEYMAP_FORMAT_TEXT_V1);lab.size=strlen(s)+1;lab.keymap=memfd_create("lab-keymap",MFD_CLOEXEC);if(lab.keymap<0 || write(lab.keymap,s,lab.size)!=lab.size)return 2;free(s);xkb_keymap_unref(map);xkb_context_unref(ctx);lab.display=wl_display_create();if(!lab.display)return 2;wl_global_create(lab.display,&wl_seat_interface,7,nullptr,bindSeat);wl_global_create(lab.display,&zwp_input_method_manager_v2_interface,1,nullptr,bindIM);wl_global_create(lab.display,&zwp_virtual_keyboard_manager_v1_interface,1,nullptr,bindVK);if(wl_display_add_socket(lab.display,"wayland-lab"))return 2;lab.hold=wl_event_loop_add_timer(wl_display_get_event_loop(lab.display),rawRelease,nullptr);if(!lab.hold)return 2;lab.settle=wl_event_loop_add_timer(wl_display_get_event_loop(lab.display),settled,nullptr);if(!lab.settle)return 2;auto*t=wl_event_loop_add_timer(wl_display_get_event_loop(lab.display),deadline,nullptr);wl_event_source_timer_update(t,5000);puts("{\"ready\":true}");fflush(stdout);wl_display_run(lab.display);bool modifierProof=lab.modifierDown && lab.modifierUp && lab.shiftAtPress && lab.shiftAtRelease && lab.vkModifierMask==0 && lab.modifierWaitedForAck && lab.releaseSent && lab.acknowledged==lab.doneCount && lab.grabs==1 && lab.released==0 && lab.press==1 && lab.release==1;bool ok=(!lab.modifier || modifierProof) && !lab.failed && lab.raced && ((lab.ab || lab.cross) ? ((lab.grabs==1 && lab.released==0)||(lab.grabs==2 && lab.released==1)) : (lab.retained ? (lab.grabs==1 && lab.released==0) : (lab.grabs==2 && lab.released==1))) && lab.release==(lab.fresh?2u:1u) && (!lab.inactiveRaw || (lab.rawReleaseSent && lab.acknowledged==lab.doneCount)) && (!lab.ab || lab.releaseSent) && (!lab.gated || (lab.flushed && lab.acknowledged==lab.doneCount && lab.waitedForAck));printf("{\"completed\":%s,\"vkPress\":%u,\"vkRelease\":%u,\"oneLiveGrabInvariant\":%s,\"grabs\":%u,\"releasedGrabs\":%u,\"commits\":%u,\"vkReady\":%s,\"raced\":%s,\"ackMatched\":%s,\"gatedPairSent\":%s,\"waitedForAck\":%s,\"releaseAfterAck\":%s,\"inactiveRawPairCompleted\":%s,\"crossTransitionSent\":%s,\"modifierDownObserved\":%s,\"modifierUpObserved\":%s,\"shiftAtBothKeys\":%s,\"modifierReturnedToZero\":%s,\"modifierWaitedForAck\":%s}\n",ok?"true":"false",lab.press,lab.release,lab.failed?"false":"true",lab.grabs,lab.released,lab.commits,lab.vkReady?"true":"false",lab.raced?"true":"false",lab.acknowledged==lab.doneCount?"true":"false",lab.flushed?"true":"false",lab.waitedForAck?"true":"false",lab.releaseSent?"true":"false",lab.rawReleaseSent?"true":"false",lab.crossTransition?"true":"false",lab.modifierDown?"true":"false",lab.modifierUp?"true":"false",(lab.shiftAtPress && lab.shiftAtRelease)?"true":"false",(lab.modifierUp && lab.vkModifierMask==0)?"true":"false",lab.modifierWaitedForAck?"true":"false");fflush(stdout);if(ok && getchar()!='\n')ok=false;lab.cleaning=true;wl_display_destroy_clients(lab.display);wl_display_destroy(lab.display);close(lab.keymap);return ok?0:2;}
