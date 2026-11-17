// SPDX-License-Identifier: MIT
// Closed native Wayland provenance + lifecycle experiment: two owned
// xdg_toplevels on a private Sway socket, observed by two independent
// ext_foreign_toplevel_list_v1 connections. Identity is the protocol
// identifier string, never proxy IDs. After the initial match, native window A
// is destroyed (observers must see exactly one closed handle and one matching
// survivor), then recreated (observers must see two matching live IDs, with
// the retired ID absent and exactly one ID new relative to the initial set).
// No identifier or title text is ever emitted.
#include <wayland-client.h>
#include "xdg-shell-client.h"
#include "foreign-list-client.h"

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr const char* kSocket = "/run/lab/wayland-lab";
constexpr const char* kAppId = "rasklad.owned-provenance";
constexpr const char* const kTitles[2] = {"synthetic-window-A", "synthetic-window-B"};
constexpr int kMaxIter = 100;
constexpr std::size_t kMaxHandles = 8;
constexpr int kW = 16, kH = 16, kStride = kW * 4, kSize = kStride * kH;

void on_alarm(int) {
  static const char msg[] =
      "{\"schema\":1,\"ok\":false,\"nativeToplevels\":true,"
      "\"fcitxLoaded\":false,\"fieldBindingVerified\":false,\"timeout\":true}\n";
  ssize_t r = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
  (void)r;
  _exit(1);
}

struct App {
  wl_display* dpy = nullptr;
  wl_registry* reg = nullptr;
  wl_compositor* comp = nullptr;
  wl_shm* shm = nullptr;
  xdg_wm_base* wm = nullptr;
  wl_buffer* buf = nullptr;  // immutable, shared by all surfaces
  bool failed = false;
};

struct Win {
  App* app = nullptr;
  wl_surface* surf = nullptr;
  xdg_surface* xs = nullptr;
  xdg_toplevel* top = nullptr;
  bool mapped = false;
};

void wm_ping(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener wm_listener = {wm_ping};

void app_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t) {
  App* a = static_cast<App*>(data);
  if (!a->comp && std::strcmp(iface, wl_compositor_interface.name) == 0) {
    a->comp = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, 1));
  } else if (!a->shm && std::strcmp(iface, wl_shm_interface.name) == 0) {
    a->shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
  } else if (!a->wm && std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
    a->wm = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 1));
    if (a->wm) xdg_wm_base_add_listener(a->wm, &wm_listener, a);
  }
}
void global_remove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener app_reg_listener = {app_global, global_remove};

void xs_configure(void* data, xdg_surface* xs, uint32_t serial) {
  Win* w = static_cast<Win*>(data);
  xdg_surface_ack_configure(xs, serial);
  if (!w->app->buf) { w->app->failed = true; return; }
  wl_surface_attach(w->surf, w->app->buf, 0, 0);
  wl_surface_damage(w->surf, 0, 0, kW, kH);
  wl_surface_commit(w->surf);
  w->mapped = true;
}
const xdg_surface_listener xs_listener = {xs_configure};

void top_configure(void*, xdg_toplevel*, int32_t, int32_t, wl_array*) {}
void top_close(void*, xdg_toplevel*) {}
const xdg_toplevel_listener top_listener = [] {
  xdg_toplevel_listener value{}; value.configure=top_configure; value.close=top_close; return value;
}();

// Destroys a window's protocol objects (child-first) and resets it. Safe on a
// partially created or already destroyed window.
void destroy_win(Win& w) {
  if (w.top) xdg_toplevel_destroy(w.top);
  if (w.xs) xdg_surface_destroy(w.xs);
  if (w.surf) wl_surface_destroy(w.surf);
  w.top = nullptr;
  w.xs = nullptr;
  w.surf = nullptr;
  w.mapped = false;
}

// Creates an unmapped owned toplevel; mapping happens on first configure.
// On failure, any partial objects are released and false is returned.
bool create_win(App& app, Win& w, const char* title) {
  destroy_win(w);
  w.app = &app;
  w.surf = wl_compositor_create_surface(app.comp);
  w.xs = w.surf ? xdg_wm_base_get_xdg_surface(app.wm, w.surf) : nullptr;
  w.top = w.xs ? xdg_surface_get_toplevel(w.xs) : nullptr;
  if (!w.top) { destroy_win(w); return false; }
  xdg_surface_add_listener(w.xs, &xs_listener, &w);
  xdg_toplevel_add_listener(w.top, &top_listener, &w);
  xdg_toplevel_set_app_id(w.top, kAppId);
  xdg_toplevel_set_title(w.top, title);
  wl_surface_commit(w.surf);
  return true;
}

struct Handle {
  ext_foreign_toplevel_handle_v1* h = nullptr;
  std::string id, appId;
  bool done = false, closed = false, badId = false;
};

struct Obs {
  wl_display* dpy = nullptr;
  wl_registry* reg = nullptr;
  ext_foreign_toplevel_list_v1* list = nullptr;
  std::vector<std::unique_ptr<Handle>> handles;
  bool finished = false, overflow = false;
};

void h_closed(void* d, ext_foreign_toplevel_handle_v1* handle) {
  Handle* h=static_cast<Handle*>(d); h->closed=true;
  ext_foreign_toplevel_handle_v1_destroy(handle); h->h=nullptr;
}
void h_done(void* d, ext_foreign_toplevel_handle_v1*) { static_cast<Handle*>(d)->done = true; }
void h_title(void*, ext_foreign_toplevel_handle_v1*, const char*) {}
void h_app_id(void* d, ext_foreign_toplevel_handle_v1*, const char* s) {
  static_cast<Handle*>(d)->appId = s ? s : "";
}
void h_identifier(void* d, ext_foreign_toplevel_handle_v1*, const char* s) {
  Handle* h = static_cast<Handle*>(d);
  if (!h->id.empty()) h->badId = true;  // must only be sent once, at creation
  h->id = s ? s : "";
}
const ext_foreign_toplevel_handle_v1_listener h_listener = {h_closed, h_done, h_title, h_app_id,
                                                            h_identifier};

void l_toplevel(void* d, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* th) {
  Obs* o = static_cast<Obs*>(d);
  if (o->handles.size() >= kMaxHandles) {
    o->overflow = true;
    ext_foreign_toplevel_handle_v1_destroy(th);
    return;
  }
  o->handles.emplace_back(new Handle());
  o->handles.back()->h = th;
  ext_foreign_toplevel_handle_v1_add_listener(th, &h_listener, o->handles.back().get());
}
void l_finished(void* d, ext_foreign_toplevel_list_v1*) { static_cast<Obs*>(d)->finished = true; }
const ext_foreign_toplevel_list_v1_listener l_listener = {l_toplevel, l_finished};

void obs_global(void* data, wl_registry* reg, uint32_t name, const char* iface, uint32_t) {
  Obs* o = static_cast<Obs*>(data);
  if (o->list || std::strcmp(iface, ext_foreign_toplevel_list_v1_interface.name) != 0) return;
  o->list = static_cast<ext_foreign_toplevel_list_v1*>(
      wl_registry_bind(reg, name, &ext_foreign_toplevel_list_v1_interface, 1));
  if (o->list) ext_foreign_toplevel_list_v1_add_listener(o->list, &l_listener, o);
}
const wl_registry_listener obs_reg_listener = {obs_global, global_remove};

bool printable_id(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  for (unsigned char c : s)
    if (c < 0x20 || c > 0x7e) return false;
  return true;
}

struct Summary {
  int owned = 0;   // live (done, not closed) owned handles
  int closed = 0;  // closed owned handles
  bool valid = true;
  std::set<std::string> ids;  // live owned identifiers
};

Summary summarize(const Obs& o) {
  Summary s;
  if (o.overflow) s.valid = false;
  for (const auto& h : o.handles) {
    if (!h->done || h->appId != kAppId) continue;
    if (h->badId || !printable_id(h->id)) s.valid = false;
    if (h->closed) { ++s.closed; continue; }
    ++s.owned;
    if (!s.ids.insert(h->id).second) s.valid = false;
  }
  return s;
}

}  // namespace

namespace registry_only {
struct Caps { uint32_t stable=0,wlr=0,mapping=0; };
void global(void* data,wl_registry*,uint32_t,const char* name,uint32_t version) {
  auto* c=static_cast<Caps*>(data);
  if (!std::strcmp(name,"ext_foreign_toplevel_list_v1")) c->stable=version;
  if (!std::strcmp(name,"zwlr_foreign_toplevel_manager_v1")) c->wlr=version;
  if (!std::strcmp(name,"hyprland_toplevel_mapping_manager_v1")) c->mapping=version;
}
void removed(void*,wl_registry*,uint32_t) {}
const wl_registry_listener listener={global,removed};
int run(int fd) {
  int type=0; socklen_t length=sizeof(type);
  if (getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&length) || type!=SOCK_STREAM) return 2;
  wl_display* display=wl_display_connect_to_fd(fd);
  if (!display) return 2;
  Caps caps;wl_registry* registry=wl_display_get_registry(display);
  bool ok=registry && wl_registry_add_listener(registry,&listener,&caps)==0;
  if (ok) ok=wl_display_roundtrip(display)>=0;
  if (registry) wl_registry_destroy(registry);
  wl_display_disconnect(display);
  std::printf("{\"schema\":1,\"ok\":%s,\"registryOnly\":true,\"globalsBound\":0,"
    "\"stableListVersion\":%u,\"wlrManagerVersion\":%u,\"hyprlandMappingVersion\":%u,"
    "\"nativeToplevels\":false,\"fcitxLoaded\":false,\"fieldBindingVerified\":false}\n",
    ok?"true":"false",caps.stable,caps.wlr,caps.mapping);
  return ok?0:1;
}
}

int main(int argc,char** argv) {
  signal(SIGALRM, on_alarm);
  alarm(6);
  if (argc!=1) {
    const char* prefix="--registry-fd=";
    if (argc!=2 || std::strncmp(argv[1],prefix,std::strlen(prefix))) return 2;
    char* end=nullptr;errno=0;long fd=std::strtol(argv[1]+std::strlen(prefix),&end,10);
    if (errno || !end || *end || fd<3 || fd>1023) return 2;
    return registry_only::run(static_cast<int>(fd));
  }
  App app;
  Win wins[2];
  Obs obs[2];
  int memfd = -1;
  void* map = MAP_FAILED;
  bool ok = false, cross = false;
  bool initialMatch = false, closedObservedBoth = false, remapMatch = false,
       retiredIdNotReused = false;
  int iters = 0;
  Summary s0, s1;

  // One shared-budget step: roundtrip app and both observers, then summarize.
  auto step = [&]() -> bool {
    if (wl_display_roundtrip(app.dpy) < 0 || app.failed) return false;
    for (Obs& o : obs)
      if (wl_display_roundtrip(o.dpy) < 0 || o.finished) return false;
    s0 = summarize(obs[0]);
    s1 = summarize(obs[1]);
    return true;
  };
  // Steps until pred() holds or the shared budget is exhausted.
  auto wait_until = [&](const std::function<bool()>& pred) -> bool {
    for (; iters < kMaxIter; ) {
      ++iters;
      if (!step()) return false;
      if (pred()) return true;
    }
    return false;
  };

  do {  // single pass; every break fails closed
    app.dpy = wl_display_connect(kSocket);
    if (!app.dpy) break;
    app.reg = wl_display_get_registry(app.dpy);
    if (!app.reg || wl_registry_add_listener(app.reg, &app_reg_listener, &app) != 0) break;
    if (wl_display_roundtrip(app.dpy) < 0 || !app.comp || !app.shm || !app.wm) break;
    memfd = memfd_create("rasklad-shm", MFD_CLOEXEC);
    if (memfd < 0 || ftruncate(memfd, kSize) != 0) break;
    map = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (map == MAP_FAILED) break;
    uint32_t* px = static_cast<uint32_t*>(map);
    for (int i = 0; i < kW * kH; ++i) px[i] = 0xff3366aau;
    wl_shm_pool* pool = wl_shm_create_pool(app.shm, memfd, kSize);
    if (!pool) break;
    app.buf = wl_shm_pool_create_buffer(pool, 0, kW, kH, kStride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    if (!app.buf) break;

    if (!create_win(app, wins[0], kTitles[0]) || !create_win(app, wins[1], kTitles[1])) break;
    bool err = false;
    for (; iters < kMaxIter && !(wins[0].mapped && wins[1].mapped); ++iters)
      if (wl_display_roundtrip(app.dpy) < 0 || app.failed) { err = true; break; }
    if (err || !(wins[0].mapped && wins[1].mapped)) break;

    for (Obs& o : obs) {
      o.dpy = wl_display_connect(kSocket);
      o.reg = o.dpy ? wl_display_get_registry(o.dpy) : nullptr;
      if (!o.reg || wl_registry_add_listener(o.reg, &obs_reg_listener, &o) != 0) { err = true; break; }
      if (wl_display_roundtrip(o.dpy) < 0 || !o.list) { err = true; break; }
    }
    if (err) break;

    // Phase 1: initial two-window match.
    if (!wait_until([&] {
          return s0.valid && s1.valid && s0.owned == 2 && s1.owned == 2 && s0.closed == 0 &&
                 s1.closed == 0;
        }))
      break;
    cross = (s0.ids == s1.ids);
    initialMatch = cross;
    if (!initialMatch) break;
    const std::set<std::string> initialIds = s0.ids;

    // Phase 2: destroy ONLY native window A.
    destroy_win(wins[0]);
    if (!wait_until([&] {
          return s0.valid && s1.valid && s0.closed == 1 && s1.closed == 1 && s0.owned == 1 &&
                 s1.owned == 1;
        }))
      break;
    if (s0.ids != s1.ids) break;
    const std::string survivorId = *s0.ids.begin();
    if (!initialIds.count(survivorId)) break;
    std::string retiredId;
    for (const std::string& id : initialIds)
      if (id != survivorId) retiredId = id;
    if (retiredId.empty()) break;
    closedObservedBoth = true;

    // Phase 3: recreate native A with the same app_id/title.
    if (!create_win(app, wins[0], kTitles[0])) break;
    if (!wait_until([&] {
          return wins[0].mapped && wins[1].mapped && s0.valid && s1.valid && s0.owned == 2 &&
                 s1.owned == 2;
        }))
      break;
    if (s0.ids != s1.ids || s0.closed != 1 || s1.closed != 1) break;
    if (!s0.ids.count(survivorId)) break;
    remapMatch = true;
    retiredIdNotReused = !s0.ids.count(retiredId);
    int newIds = 0;
    for (const std::string& id : s0.ids)
      if (!initialIds.count(id)) ++newIds;
    ok = initialMatch && closedObservedBoth && remapMatch && retiredIdNotReused && newIds == 1;
  } while (false);

  bool cleanupOk=true;
  for (Obs& o : obs) {
    if (o.list && o.dpy && !o.finished) {
      ext_foreign_toplevel_list_v1_stop(o.list);
      for (int n=0;n<4 && !o.finished;++n) {
        if (wl_display_roundtrip(o.dpy)<0) { cleanupOk=false; break; }
      }
      if (!o.finished) cleanupOk=false;
    }
    for (auto& h : o.handles)
      if (h->h) ext_foreign_toplevel_handle_v1_destroy(h->h);
    o.handles.clear();
    if (o.list) ext_foreign_toplevel_list_v1_destroy(o.list);
    if (o.reg) wl_registry_destroy(o.reg);
    if (o.dpy) { wl_display_flush(o.dpy); wl_display_disconnect(o.dpy); }
    o.list = nullptr; o.reg = nullptr; o.dpy = nullptr;
  }
  ok=ok && cleanupOk;
  int mappedCount = int(wins[0].mapped) + int(wins[1].mapped);
  for (Win& w : wins) destroy_win(w);
  if (app.buf) wl_buffer_destroy(app.buf);
  if (app.wm) xdg_wm_base_destroy(app.wm);
  if (app.shm) wl_shm_destroy(app.shm);
  if (app.comp) wl_compositor_destroy(app.comp);
  if (app.reg) wl_registry_destroy(app.reg);
  if (app.dpy) { wl_display_flush(app.dpy); wl_display_disconnect(app.dpy); }
  if (map != MAP_FAILED) munmap(map, kSize);
  if (memfd >= 0) close(memfd);
  alarm(0);
  auto b = [](bool v) { return v ? "true" : "false"; };
  std::printf("{\"schema\":1,\"ok\":%s,\"nativeToplevels\":true,\"fcitxLoaded\":false,"
              "\"fieldBindingVerified\":false,\"appWindowsMapped\":%d,\"observers\":2,"
              "\"observerOwnedCounts\":[%d,%d],\"observerClosedCounts\":[%d,%d],"
              "\"identifiersValid\":%s,\"crossObserverMatch\":%s,\"initialMatch\":%s,"
              "\"closedObservedBoth\":%s,\"remapMatch\":%s,\"retiredIdNotReused\":%s,"
              "\"observerCleanup\":%s,\"iterations\":%d}\n",
              b(ok), mappedCount, s0.owned, s1.owned, s0.closed, s1.closed,
              b(s0.valid && s1.valid), b(cross), b(initialMatch), b(closedObservedBoth),
              b(remapMatch), b(retiredIdNotReused), b(cleanupOk), iters);
  return ok ? 0 : 1;
}
