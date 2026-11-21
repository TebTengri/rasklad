// SPDX-License-Identifier: MIT
// Actual logger callback calibration against synthetic public message metadata.
// No real display, Fcitx, client, socket, device, protocol strings or key logs.
#include "server_route_fake.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

namespace {
using Words=std::array<std::uint32_t,20>;
using Snapshot=std::uint32_t(*)(std::uint32_t*,std::uint32_t) noexcept;
Snapshot snapshot=nullptr;
ServerRouteFake *state=nullptr;
int managerOpcode=-1;

bool stableFile(Words *result=nullptr){
    Words first{},second{},api{};
    const int fd=open("/run/lab/server-route-witness.bin",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return false;
    struct stat info{};
    const bool fileOK=fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&info.st_size==80
        &&(info.st_mode&0777)==0600&&info.st_uid==getuid()&&info.st_nlink==1;
    const auto one=pread(fd,first.data(),80,0),two=pread(fd,second.data(),80,0);close(fd);
    if(!fileOK||one!=80||two!=80||first!=second||(first[2]&1)||first[0]!=0x52535731u||first[1]!=1||first[18]!=0||first[19]!=0)return false;
    if(snapshot(api.data(),20)!=20||api!=first)return false;
    if(result)*result=first;
    return true;
}

bool attach(){
    wl_display *display=wl_display_create();
    Words out{};
    return display!=nullptr&&display==state->lastDisplay&&state->originalCalls==1&&state->loggerCalls==1
        &&state->logger&&stableFile(&out)&&out[3]==1&&out[4]==1&&out[5]==1&&out[17]==1;
}

void message(wl_protocol_logger_type direction,const char *kind,const char *name,
             const char *signature,int opcode,int count,const wl_argument *arguments){
    ServerRouteResource resource{kind};
    wl_message definition{name,signature,nullptr};
    wl_protocol_logger_message item{asResource(resource),opcode,&definition,count,arguments};
    state->logger(state->loggerData,direction,&item);
}
void key(wl_protocol_logger_type direction,const char *kind,int opcode,std::uint32_t stateValue){
    wl_argument arguments[4]{};
    const bool vk=direction==WL_PROTOCOL_LOGGER_REQUEST;
    arguments[vk?1:2].u=57;arguments[vk?2:3].u=stateValue;
    message(direction,kind,"key",vk?"uuu":"uuuu",opcode,vk?3:4,arguments);
}
void lifecycle(){
    const auto *inaccessible=reinterpret_cast<const wl_argument*>(1);
    message(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_v2","deactivate","",1,0,inaccessible);
    message(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_v2","done","",5,0,inaccessible);
    message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_input_method_keyboard_grab_v2","release","",0,0,inaccessible);
    message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_input_method_manager_v2","get_input_method","on",managerOpcode,2,inaccessible);
}

bool protectedNonSpace(){
    const long page=sysconf(_SC_PAGESIZE);
    if(page<=0)return false;
    auto *memory=static_cast<unsigned char*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0));
    if(memory==MAP_FAILED)return false;
    if(mprotect(memory+page,page,PROT_NONE)!=0){munmap(memory,page*2);return false;}
    // The key is readable. The state index falls on the protected page and
    // must not be touched when the key is not Space.
    auto *vk=reinterpret_cast<wl_argument*>(memory+page-sizeof(wl_argument)*2);
    vk[1].u=30;
    message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1","key","uuu",1,3,vk);
    auto *grab=reinterpret_cast<wl_argument*>(memory+page-sizeof(wl_argument)*3);
    grab[2].u=30;
    message(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_keyboard_grab_v2","key","uuuu",1,4,grab);
    message(WL_PROTOCOL_LOGGER_EVENT,"wl_keyboard","key","uuuu",3,4,grab);
    munmap(memory,page*2);return true;
}
}

int main(int argc,char **argv){
    void *raw=dlsym(RTLD_DEFAULT,"rasklad_server_route_snapshot_v1");
    static_assert(sizeof(raw)==sizeof(snapshot));std::memcpy(&snapshot,&raw,sizeof(snapshot));
    using State=ServerRouteFake*(*)();State fake=nullptr;
    raw=dlsym(RTLD_DEFAULT,"rasklad_server_fake_v1");std::memcpy(&fake,&raw,sizeof(fake));
    if(!snapshot||!fake||std::getenv("LD_PRELOAD")!=nullptr||argc!=2)return 1;
    // The runner computes this from the hash-pinned upstream XML. No opcode
    // fallback is allowed: sharing a guessed literal with the shim hid a bug.
    const char *opcodeText=std::getenv("RASKLAD_UNIT_MANAGER_OPCODE");
    if(!opcodeText||!*opcodeText)return 26;
    unsigned parsedOpcode=0;
    for(const char *p=opcodeText;*p;++p){
        if(*p<'0'||*p>'9')return 26;
        parsedOpcode=parsedOpcode*10+static_cast<unsigned>(*p-'0');
        if(parsedOpcode>255)return 26;
    }
    managerOpcode=static_cast<int>(parsedOpcode);
    state=fake();Words out{},denied{};denied.fill(0xa5a5a5a5u);
    if(snapshot(nullptr,20)!=0||snapshot(denied.data(),19)!=0)return 2;
    for(auto word:denied)if(word!=0xa5a5a5a5u)return 2;
    if(!stableFile(&out)
        ||out[2]!=0||out[3]!=0||out[4]!=1||out[5]!=0||out[17]!=0)return 2;
    const char *mode=argv[1];
    if(std::strcmp(mode,"null")==0){
        state->nullOriginal=true;
        if(wl_display_create()!=nullptr||state->originalCalls!=1||state->loggerCalls!=0||!stableFile(&out))return 3;
        return out[3]==0&&out[4]==0&&out[5]==1&&out[17]==0?0:4;
    }
    if(std::strcmp(mode,"attach-failure")==0){
        state->failLogger=true;
        auto *display=wl_display_create();
        if(!display||display!=state->lastDisplay||state->originalCalls!=1||state->loggerCalls!=1||!stableFile(&out))return 5;
        return out[3]==0&&out[4]==0&&out[5]==1&&out[17]==0?0:6;
    }
    if(std::strcmp(mode,"foreign-hook")==0){std::thread worker([]{wl_display_create();});worker.join();return 7;}
    if(!attach())return 8;
    if(std::strcmp(mode,"ready")==0){
        Words before{},after{};if(!stableFile(&before)||!stableFile(&after))return 9;
        return before==after?0:10;
    }
    if(std::strcmp(mode,"counts")==0){
        for(auto stateValue:{1u,0u}){
            key(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1",1,stateValue);
            key(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_keyboard_grab_v2",1,stateValue);
            key(WL_PROTOCOL_LOGGER_EVENT,"wl_keyboard",3,stateValue);
        }
        lifecycle();
        if(!stableFile(&out)||out[4]!=1||out[2]==0)return 11;
        for(unsigned i=6;i<=11;++i)if(out[i]!=1)return 12;
        for(unsigned i=13;i<=16;++i)if(out[i]!=1)return 13;
        return out[12]==0&&out[17]==1?0:14;
    }
    if(std::strcmp(mode,"noise")==0){
        const auto *inaccessible=reinterpret_cast<const wl_argument*>(1);
        message(WL_PROTOCOL_LOGGER_REQUEST,"wl_surface","commit","",6,0,inaccessible);
        message(WL_PROTOCOL_LOGGER_EVENT,"wl_keyboard","enter","uoa",1,3,inaccessible);
        message(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_v2","surrounding_text","suu",2,3,inaccessible);
        message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_input_method_v2","set_preedit_string","sii",1,3,inaccessible);
        message(WL_PROTOCOL_LOGGER_REQUEST,"unrelated_interface","key","uuu",1,3,inaccessible);
        if(!protectedNonSpace()||!stableFile(&out)||out[4]!=1)return 15;
        for(unsigned i=6;i<=16;++i)if(out[i]!=0)return 16;
        return 0;
    }
    if(std::strcmp(mode,"malformed")==0){
        const auto *inaccessible=reinterpret_cast<const wl_argument*>(1);
        message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1","key","uuu",0,3,inaccessible);
        message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1","key","uuu",1,4,inaccessible);
        message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1","key","suu",1,3,inaccessible);
        message(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1","key","uuu",1,3,nullptr);
        message(WL_PROTOCOL_LOGGER_EVENT,"zwp_input_method_v2","done","u",5,0,inaccessible);
        if(!stableFile(&out)||out[4]!=0)return 17;
        for(unsigned i=6;i<=16;++i)if(out[i]!=0)return 18;
        key(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1",1,1);
        return stableFile(&out)&&out[4]==0&&out[6]==1?0:18;
    }
    if(std::strcmp(mode,"unknown")==0){
        key(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1",1,7);
        if(!stableFile(&out)||out[4]!=0||out[12]!=1)return 19;
        for(unsigned i=6;i<=11;++i)if(out[i]!=0)return 20;
        return 0;
    }
    if(std::strcmp(mode,"multiple")==0){
        auto *first=state->lastDisplay;auto *second=wl_display_create();
        return second&&second!=first&&second==state->lastDisplay&&state->originalCalls==2
            &&stableFile(&out)&&out[5]==2&&out[4]==0?0:21;
    }
    if(std::strcmp(mode,"foreign-snapshot")==0){
        bool refused=false;std::thread worker([&]{Words other{};other.fill(0xa5a5a5a5u);refused=snapshot(other.data(),20)==0;
            for(auto word:other)refused&=word==0xa5a5a5a5u;});worker.join();
        return refused&&stableFile(&out)?0:22;
    }
    if(std::strcmp(mode,"foreign-callback")==0){
        std::thread worker([]{key(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1",1,1);});worker.join();return 23;
    }
    if(std::strcmp(mode,"saturation")==0){
        for(unsigned i=0;i<65540;++i)key(WL_PROTOCOL_LOGGER_REQUEST,"zwp_virtual_keyboard_v1",1,1);
        return stableFile(&out)&&out[6]==65535&&out[4]==0?0:24;
    }
    return 25;
}
