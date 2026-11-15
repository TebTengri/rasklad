// SPDX-License-Identifier: MIT
// Persistent synthetic resources; no display/socket/client/device is created.
#include "server_route_fake.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
namespace {
using Words=std::array<uint32_t,28>;
using Snapshot=uint32_t(*)(uint32_t*,uint32_t) noexcept;
Snapshot snapshot;
ServerRouteFake *fake;
using Destroy=void(*)(wl_resource*);
Destroy destroyResource;
ServerRouteResource manager{"zwp_input_method_manager_v2"},im{"zwp_input_method_v2"};
ServerRouteResource grab{"zwp_input_method_keyboard_grab_v2"},other{"zwp_input_method_keyboard_grab_v2"};
bool read(Words &out){
    Words again{},api{};int fd=open("/run/lab/server-route-witness.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return false;
    struct stat st{};bool ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size==112&&(st.st_mode&0777)==0600&&st.st_uid==getuid()&&st.st_nlink==1;
    ok=ok&&pread(fd,out.data(),112,0)==112&&pread(fd,again.data(),112,0)==112;close(fd);
    return ok&&out==again&&!(out[2]&1)&&out[0]==0x52535731&&out[1]==2&&snapshot(api.data(),28)==28&&api==out
        &&uint64_t(out[18])+out[20]+out[22]==out[8]
        &&uint64_t(out[19])+out[21]+out[23]==out[9];
}
void send(ServerRouteResource &r,const char *id,const char *name,bool request,int count=0,const wl_argument *args=reinterpret_cast<const wl_argument*>(1)){
    char op[100],sig[100];snprintf(op,sizeof(op),"RASKLAD_UNIT_%s_OPCODE",id);snprintf(sig,sizeof(sig),"RASKLAD_UNIT_%s_SIGNATURE",id);
    const char *o=getenv(op),*s=getenv(sig);if(!o||!s)_exit(90);
    wl_message def{name,s,nullptr};wl_protocol_logger_message item{asResource(r),atoi(o),&def,count,args};
    fake->logger(fake->loggerData,request?WL_PROTOCOL_LOGGER_REQUEST:WL_PROTOCOL_LOGGER_EVENT,&item);
}
void key(ServerRouteResource &r=grab,uint32_t state=1){wl_argument a[4]{};a[2].u=57;a[3].u=state;send(r,"KEY","key",false,4,a);}
void keymap(ServerRouteResource &r=grab){send(r,"KEYMAP","keymap",false,3);}
void done(){send(im,"DONE","done",false);}
void deactivate(){send(im,"DEACTIVATE","deactivate",false);}
void setup(){send(manager,"MANAGER","get_input_method",true,2);send(im,"ACTIVATE","activate",false);keymap();}
template<class T> T symbol(const char *name){void *p=dlsym(RTLD_DEFAULT,name);T f{};static_assert(sizeof(f)==sizeof(p));memcpy(&f,&p,sizeof(f));return f;}
}
int main(int argc,char **argv){
    if(argc!=2||getenv("LD_PRELOAD"))return 1;
    snapshot=symbol<Snapshot>("rasklad_server_route_snapshot_v2");auto get=symbol<ServerRouteFake*(*)()>("rasklad_server_fake_v1");destroyResource=symbol<Destroy>("rasklad_server_fake_destroy");
    if(!snapshot||!get||!destroyResource)return 2;fake=get();Words out{},denied{};denied.fill(0xabababab);
    if(snapshot(nullptr,28)||snapshot(denied.data(),27))return 3;for(auto n:denied)if(n!=0xabababab)return 3;
    if(!read(out)||out[4]!=1)return 4;for(unsigned i=18;i<28;++i)if(out[i])return 4;
    if(!wl_display_create()||!fake->logger||!read(out))return 5;
    const char *mode=argv[1];
    if(!strcmp(mode,"ready"))return 0;
    setup();
    if(!strcmp(mode,"epoch")){key();deactivate();key();done();key();key(grab,0);keymap();key();return read(out)&&out[4]==1&&out[18]==2&&out[19]==1&&out[20]==2&&out[26]==1?0:10;}
    if(!strcmp(mode,"release")){send(grab,"RELEASE","release",true);key();destroyResource(asResource(grab));return read(out)&&out[4]==1&&out[20]==1&&out[26]==1&&out[27]==1?0:11;}
    if(!strcmp(mode,"aba")){deactivate();done();destroyResource(asResource(grab));keymap();key();return read(out)&&out[4]==1&&out[18]==0&&out[20]==1&&out[26]==2&&out[27]==1?0:12;}
    if(!strcmp(mode,"overlap")){keymap(other);key();destroyResource(asResource(other));key();return read(out)&&out[4]==1&&out[24]==1&&out[25]==2&&out[26]==2&&out[27]==1?0:13;}
    if(!strcmp(mode,"missing")){key(other);key(other,0);return read(out)&&!out[4]&&out[22]==1&&out[23]==1?0:14;}
    if(!strcmp(mode,"mismatch")){grab.client=reinterpret_cast<wl_client*>(0x4560);key();return read(out)&&!out[4]&&out[22]==1?0:15;}
    if(!strcmp(mode,"multiple-im")){ServerRouteResource im2{"zwp_input_method_v2"};send(im2,"ACTIVATE","activate",false);return read(out)&&!out[4]?0:16;}
    if(!strcmp(mode,"overflow")){std::array<ServerRouteResource,128> many{};for(auto &r:many){r.className=grab.className;keymap(r);}return read(out)&&!out[4]&&out[26]==128&&out[25]==128?0:17;}
    if(!strcmp(mode,"foreign-destroy")){std::thread t([]{destroyResource(asResource(grab));});t.join();return 18;}
    if(!strcmp(mode,"noise")){ServerRouteResource r{"wl_surface"};wl_message d{"commit","",nullptr};wl_protocol_logger_message m{asResource(r),6,&d,0,reinterpret_cast<const wl_argument*>(1)};Words before{};if(!read(before))return 19;fake->logger(fake->loggerData,WL_PROTOCOL_LOGGER_REQUEST,&m);return read(out)&&out==before?0:19;}
    return 99;
}
