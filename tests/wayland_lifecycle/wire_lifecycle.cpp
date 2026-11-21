// SPDX-License-Identifier: MIT
// Private real-wire lifetime contrast; no text input, host socket or IME implementation.
#include "wire_common.h"
#include "wire-server.h"
#include <wayland-server-core.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>

namespace {
struct Server {
    wl_resource *live=nullptr;
    unsigned created=0,liveCount=0,peak=0,press=0,release=0;
    bool begun=false,invalid=false;
};
std::int64_t nowMs(){
    timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return std::int64_t(t.tv_sec)*1000+t.tv_nsec/1000000;
}
void destroyed(wl_resource *r){
    auto *s=static_cast<Server*>(wl_resource_get_user_data(r));
    if(s->live!=r||s->liveCount!=1)s->invalid=true;
    s->live=nullptr;s->liveCount=0;
}
void releaseGrab(wl_client*,wl_resource *r){wl_resource_destroy(r);}
const struct rasklad_wire_grab_v1_interface grabImpl={releaseGrab};
void getGrab(wl_client *client,wl_resource *control,std::uint32_t id){
    auto *s=static_cast<Server*>(wl_resource_get_user_data(control));
    if(s->live||s->created>=2){
        s->invalid=true;wl_resource_post_error(control,0,"one live synthetic grab required");return;
    }
    auto *r=wl_resource_create(client,&rasklad_wire_grab_v1_interface,1,id);
    if(!r){s->invalid=true;wl_client_post_no_memory(client);return;}
    s->live=r;s->liveCount=1;s->peak=1;++s->created;
    wl_resource_set_implementation(r,&grabImpl,s,destroyed);
    if(s->created==2){++s->release;rasklad_wire_grab_v1_send_key(r,0);}
}
void begin(wl_client*,wl_resource *control){
    auto *s=static_cast<Server*>(wl_resource_get_user_data(control));
    if(s->begun||!s->live||s->created!=1){
        s->invalid=true;wl_resource_post_error(control,0,"invalid synthetic phase");return;
    }
    s->begun=true;
    // Same connection, explicit wire ordering. No sleep or event fabrication.
    rasklad_wire_control_v1_send_retire(control);
    ++s->press;rasklad_wire_grab_v1_send_key(s->live,1);
}
const struct rasklad_wire_control_v1_interface controlImpl={getGrab,begin};
void bindControl(wl_client *client,void *data,std::uint32_t,std::uint32_t id){
    auto *r=wl_resource_create(client,&rasklad_wire_control_v1_interface,1,id);
    if(!r){static_cast<Server*>(data)->invalid=true;wl_client_post_no_memory(client);return;}
    wl_resource_set_implementation(r,&controlImpl,data,nullptr);
}
bool envIs(const char *name,const char *value){const char *v=getenv(name);return v&&std::strcmp(v,value)==0;}
bool waitOwn(pid_t child,int &status){pid_t got;do{got=waitpid(child,&status,0);}while(got<0&&errno==EINTR);return got==child;}
}

int main(int argc,char **argv){
    if(argc!=2||(std::strcmp(argv[1],"drop")!=0&&std::strcmp(argv[1],"drain")!=0))return 2;
    if(!envIs("RASKLAD_WIRE_LIFECYCLE_LAB","1")||!envIs("HOME","/home/lab")||
       getenv("DISPLAY")||getenv("WAYLAND_DISPLAY")||getenv("WAYLAND_SOCKET")||getenv("WAYLAND_DEBUG"))return 2;
    const bool drain=std::strcmp(argv[1],"drain")==0;
    int pair[2]{-1,-1},resultPipe[2]{-1,-1};
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return 2;
    if(pipe2(resultPipe,O_CLOEXEC|O_NONBLOCK)){close(pair[0]);close(pair[1]);return 2;}
    const pid_t parent=getpid(),child=fork();
    if(child<0){close(pair[0]);close(pair[1]);close(resultPipe[0]);close(resultPipe[1]);return 2;}
    if(child==0){
        close(pair[0]);close(resultPipe[0]);
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(3);
        WireClientResult result{};const int rc=wire_client(pair[1],drain,result);
        if(rc&&!result.failed)result.failed=1;
        ssize_t sent;
        do{sent=write(resultPipe[1],&result,sizeof(result));}while(sent<0&&errno==EINTR);
        close(resultPipe[1]);_exit(sent==sizeof(result)&&rc==0?0:3);
    }
    close(pair[1]);close(resultPipe[1]);
    Server state{};WireClientResult result{};bool received=false,ioError=false;
    auto *display=wl_display_create();
    if(display){
        auto *client=wl_client_create(display,pair[0]); // transfers the private FD
        auto *global=client?wl_global_create(display,&rasklad_wire_control_v1_interface,1,&state,bindControl):nullptr;
        if(global){
            auto *loop=wl_display_get_event_loop(display);
            pollfd fds[2]{{wl_event_loop_get_fd(loop),POLLIN,0},{resultPipe[0],POLLIN,0}};
            const auto start=nowMs(),deadline=start+5000;
            while(!received&&!ioError){
                const auto remaining=deadline-nowMs();
                if(start<0||remaining<=0)break;
                wl_display_flush_clients(display);
                const int ready=poll(fds,2,static_cast<int>(remaining));
                if(ready<0){if(errno==EINTR)continue;ioError=true;break;}
                if(ready==0)break;
                if(fds[1].revents&POLLIN){
                    ssize_t got;do{got=read(resultPipe[0],&result,sizeof(result));}while(got<0&&errno==EINTR);
                    received=got==sizeof(result);ioError=!received;break;
                }
                if(fds[1].revents&(POLLHUP|POLLERR|POLLNVAL)){ioError=true;break;}
                if(fds[0].revents&POLLIN){if(wl_event_loop_dispatch(loop,0)<0)ioError=true;}
                if(fds[0].revents&(POLLHUP|POLLERR|POLLNVAL))ioError=true;
            }
        }else ioError=true;
    }else{close(pair[0]);ioError=true;}
    close(resultPipe[0]);
    if(!received)kill(child,SIGKILL);
    int status=0;const bool waited=waitOwn(child,status);
    // Resource callbacks still refer to state; destroy before leaving its scope.
    if(display){wl_display_destroy_clients(display);wl_display_destroy(display);}
    const bool oneLive=!state.invalid&&state.peak==1&&state.created==2;
    const bool passed=received&&!ioError&&waited&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&
        !result.failed&&result.complete==1&&result.protocolError==0&&oneLive&&
        state.press==1&&state.release==1&&result.press==(drain?1u:0u)&&result.release==1;
    std::printf("{\"mode\":\"%s\",\"passed\":%s,\"serverPress\":%u,\"serverRelease\":%u,"
        "\"clientPress\":%u,\"clientRelease\":%u,\"oneLiveGrabInvariant\":%s,"
        "\"protocolError\":%u,\"barrierCompleted\":%s,"
        "\"semanticScope\":\"Wayland object lifetime only; not a Fcitx fix\"}\n",
        drain?"drain":"drop",passed?"true":"false",state.press,state.release,result.press,result.release,
        oneLive?"true":"false",result.protocolError,result.complete?"true":"false");
    return passed?0:1;
}
