// SPDX-License-Identifier: MIT
// Closed calibration of the real shim against fake libwayland registration.
// No Fcitx instance, real proxy, D-Bus, device, profile or display is created.
#include "grab_test_api.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <thread>

namespace {
using Snapshot=std::uint32_t(*)(std::uint32_t*,std::uint32_t) noexcept;
Snapshot snapshot=nullptr;
struct Expected {
    void *data=nullptr;
    zwp_input_method_keyboard_grab_v2 *grab=nullptr;
    std::uint32_t serial=101,time=202,key=57,state=1;
} expected;
bool correct=true;
unsigned callsA=0,callsB=0,otherCalls=0;
unsigned press=0,release=0,spacePress=0,spaceRelease=0,unknown=0;
std::uint32_t cap(unsigned value){return value>255?255:value;}

void checkKey(void *data,zwp_input_method_keyboard_grab_v2 *grab,std::uint32_t serial,
              std::uint32_t time,std::uint32_t key,std::uint32_t state) {
    correct &= data==expected.data && grab==expected.grab && serial==expected.serial && time==expected.time && key==expected.key && state==expected.state;
    if(state==0)++release;else ++press;
    if(key==57){if(state==0)++spaceRelease;else ++spacePress;}
    if(state!=0&&state!=1)++unknown;
    std::uint32_t out[19]{};
    // Counters must already reflect callback entry, before original dispatch.
    correct &= snapshot(out,19)==19 && out[14]==cap(press) && out[15]==cap(release)
        && out[16]==cap(spacePress) && out[17]==cap(spaceRelease) && out[18]==cap(unknown);
}
void keyA(void *d,zwp_input_method_keyboard_grab_v2 *g,std::uint32_t s,std::uint32_t t,std::uint32_t k,std::uint32_t v){++callsA;checkKey(d,g,s,t,k,v);}
void keyB(void *d,zwp_input_method_keyboard_grab_v2 *g,std::uint32_t s,std::uint32_t t,std::uint32_t k,std::uint32_t v){++callsB;checkKey(d,g,s,t,k,v);}
void keymap(void *d,zwp_input_method_keyboard_grab_v2 *g,std::uint32_t f,std::int32_t fd,std::uint32_t n){correct &= d==expected.data && g==expected.grab && f==1 && fd==-1 && n==4096;++otherCalls;}
void modifiers(void *d,zwp_input_method_keyboard_grab_v2 *g,std::uint32_t s,std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t group){correct &= d==expected.data && g==expected.grab && s==11 && a==12 && b==13 && c==14 && group==15;++otherCalls;}
void repeat(void *d,zwp_input_method_keyboard_grab_v2 *g,std::int32_t rate,std::int32_t delay){correct &= d==expected.data && g==expected.grab && rate==20 && delay==300;++otherCalls;}
zwp_input_method_keyboard_grab_v2_listener listenerA{keymap,keyA,modifiers,repeat};
zwp_input_method_keyboard_grab_v2_listener listenerB{keymap,keyB,modifiers,repeat};

int add(GrabFakeProxy &proxy,zwp_input_method_keyboard_grab_v2_listener &listener,void *data){return wl_proxy_add_listener(asProxy(proxy),asImplementation(listener),data);}
bool stats(std::uint32_t regs,std::uint32_t coverage){std::uint32_t out[19]{};return snapshot(out,19)==19&&out[0]==2&&out[1]==1&&out[12]==coverage&&out[13]==regs;}
void invoke(GrabFakeProxy &proxy,std::uint32_t key,std::uint32_t state,void *actualData){
    expected={actualData,asGrab(proxy),101,202,key,state};
    proxy.installed.key(actualData,asGrab(proxy),101,202,key,state);
}
}

int main(int argc,char **argv) {
    void *raw=dlsym(RTLD_DEFAULT,"rasklad_core_entry_snapshot_v2");
    static_assert(sizeof(raw)==sizeof(snapshot));std::memcpy(&snapshot,&raw,sizeof(snapshot));
    if(!snapshot || std::getenv("LD_PRELOAD")!=nullptr)return 1;
    std::uint32_t initial[19]{};
    if(snapshot(nullptr,19)!=0 || snapshot(initial,18)!=0 || snapshot(initial,19)!=19
       ||initial[0]!=2||initial[1]!=1||initial[12]!=1)return 2;
    for(unsigned i=2;i<19;++i)if(i!=12 && initial[i]!=0)return 3;
    if(argc!=2)return 4;
    const char *mode=argv[1];
    int registrationToken=1,actualToken=2;
    GrabFakeProxy p;
    if(std::strcmp(mode,"forward")==0){
        if(add(p,listenerA,&registrationToken)!=0 || p.registeredData!=&registrationToken
           ||p.installed.key==keyA ||p.installed.keymap!=keymap ||p.installed.modifiers!=modifiers ||p.installed.repeat_info!=repeat)return 5;
        invoke(p,57,1,&actualToken);invoke(p,57,0,&actualToken);invoke(p,30,1,&actualToken);invoke(p,57,7,&actualToken);
        p.installed.keymap(&actualToken,asGrab(p),1,-1,4096);
        p.installed.modifiers(&actualToken,asGrab(p),11,12,13,14,15);
        p.installed.repeat_info(&actualToken,asGrab(p),20,300);
        return correct&&callsA==4&&callsB==0&&otherCalls==3&&stats(1,1)?0:6;
    }
    if(std::strcmp(mode,"reuse")==0){
        if(add(p,listenerA,&registrationToken)!=0)return 7;
        const auto oldBridge=p.installed.key;
        p=GrabFakeProxy{}; // Same address, a new synthetic proxy lifetime.
        if(add(p,listenerB,&actualToken)!=0)return 8;
        expected={&registrationToken,asGrab(p),101,202,57,1};
        oldBridge(&registrationToken,asGrab(p),101,202,57,1);
        return correct&&callsA==0&&callsB==1&&stats(2,1)?0:9;
    }
    if(std::strcmp(mode,"failed")==0){
        if(add(p,listenerA,&registrationToken)!=0)return 10;
        p.result=-13;
        if(add(p,listenerB,&actualToken)!=-13)return 11;
        invoke(p,57,1,&actualToken);
        return correct&&callsA==1&&callsB==0&&stats(1,0)?0:12;
    }
    if(std::strcmp(mode,"other")==0){
        p.kind="wl_keyboard";p.version=99;p.result=-17;
        const int result=add(p,listenerA,&registrationToken);
        return result==-17&&p.received==asImplementation(listenerA)&&p.registeredData==&registrationToken&&p.versionQueries==0&&stats(0,1)?0:13;
    }
    if(std::strcmp(mode,"version")==0){
        p.version=2;p.result=-18;
        return add(p,listenerA,&registrationToken)==-18&&p.received==asImplementation(listenerA)&&p.registeredData==&registrationToken&&stats(0,0)?0:14;
    }
    if(std::strcmp(mode,"null-listener")==0){
        p.result=-19;
        return wl_proxy_add_listener(asProxy(p),nullptr,&actualToken)==-19&&p.received==nullptr&&p.registeredData==&actualToken&&stats(0,0)?0:15;
    }
    if(std::strcmp(mode,"null-key")==0){
        auto listener=listenerA;listener.key=nullptr;
        return add(p,listener,&registrationToken)==0&&p.received==asImplementation(listener)&&p.installed.key==nullptr&&p.installed.keymap==keymap&&stats(0,0)?0:16;
    }
    if(std::strcmp(mode,"overflow")==0){
        std::array<GrabFakeProxy,129> proxies;
        for(unsigned i=0;i<128;++i)if(add(proxies[i],listenerA,&registrationToken)!=0||proxies[i].installed.key==keyA)return 17;
        if(add(proxies[128],listenerA,&actualToken)!=0)return 18;
        return proxies[128].received==asImplementation(listenerA)&&proxies[128].installed.key==keyA&&stats(128,0)?0:19;
    }
    if(std::strcmp(mode,"saturation")==0){
        if(add(p,listenerA,&registrationToken)!=0)return 20;
        for(unsigned i=0;i<300;++i){invoke(p,57,1,&actualToken);invoke(p,57,0,&actualToken);}
        std::uint32_t out[19]{};
        return correct&&stats(1,1)&&snapshot(out,19)==19&&out[14]==255&&out[15]==255&&out[16]==255&&out[17]==255&&out[18]==0?0:21;
    }
    if(std::strcmp(mode,"foreign-thread")==0){
        if(add(p,listenerA,&registrationToken)!=0)return 22;
        std::thread worker([&]{p.installed.key(&actualToken,asGrab(p),101,202,57,1);});worker.join();return 23;
    }
    return 24;
}
