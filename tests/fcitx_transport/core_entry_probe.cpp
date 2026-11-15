// SPDX-License-Identifier: MIT
// No Fcitx instance, input, Wayland, D-Bus or profile is created here.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <thread>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx-utils/keysym.h>
int main(int argc,char** argv) {
    using Snapshot=std::uint32_t(*)(std::uint32_t*,std::uint32_t) noexcept;
    void *symbol=dlsym(RTLD_DEFAULT,"rasklad_core_entry_snapshot_v1");
    Snapshot snapshot=nullptr;
    static_assert(sizeof(snapshot)==sizeof(symbol));
    std::memcpy(&snapshot,&symbol,sizeof(snapshot));
    if(!snapshot||std::getenv("LD_PRELOAD")!=nullptr)return 1;
    std::uint32_t out[12]{};
    if(snapshot(nullptr,12)!=0||snapshot(out,11)!=0)return 2;
    if(snapshot(out,12)!=12||out[0]!=1||out[1]!=1)return 3;
    for(unsigned i=2;i<12;i++)if(out[i]!=0)return 4;
    if(argc==2 && std::strcmp(argv[1],"thread-entry")==0) {
        using Entry=bool(*)(fcitx::InputContext*,fcitx::KeyEvent&);
        void *raw=dlsym(RTLD_DEFAULT,"_ZN5fcitx12InputContext8keyEventERNS_8KeyEventE");
        Entry entry=nullptr;std::memcpy(&entry,&raw,sizeof(entry));if(!entry)return 9;
        fcitx::KeyEvent press(nullptr,fcitx::Key(FcitxKey_space,fcitx::KeyStates(),65),false);
        std::thread worker([&] {entry(nullptr,press);});worker.join();return 10;
    }
    if(argc==2 && std::strcmp(argv[1],"thread")==0) {
        bool rejected=false;
        std::thread worker([&] {std::uint32_t other[12]{};rejected=snapshot(other,12)==0;});
        worker.join();return rejected?0:8;
    }
    if(argc==2) {
        using Entry=bool(*)(fcitx::InputContext*,fcitx::KeyEvent&);
        using Check=std::uint32_t(*)();
        Entry entry=nullptr;Check check=nullptr;
        void *raw=dlsym(RTLD_DEFAULT,"_ZN5fcitx12InputContext8keyEventERNS_8KeyEventE");
        std::memcpy(&entry,&raw,sizeof(entry));
        raw=dlsym(RTLD_DEFAULT,"rasklad_fake_core_ok");std::memcpy(&check,&raw,sizeof(check));
        if(!entry||!check)return 5;
        fcitx::KeyEvent press(nullptr,fcitx::Key(FcitxKey_space,fcitx::KeyStates(),65),false);
        fcitx::KeyEvent release(nullptr,fcitx::Key(FcitxKey_space,fcitx::KeyStates(),65),true);
        if(entry(nullptr,press)!=false||entry(nullptr,release)!=true||check()!=1)return 6;
        if(snapshot(out,12)!=12||out[2]!=1||out[3]!=1||out[9]!=1||out[10]!=1)return 7;
    }
    return 0;
}
