// SPDX-License-Identifier: MIT
// Closed experiment: wl_surface object IDs are per-connection, not window identity.
// Uses only public libwayland APIs. Build:
//   c++ -std=c++17 object_id_scope.cpp $(pkg-config --cflags --libs wayland-client wayland-server) -pthread
#include <wayland-client.h>
#include <wayland-server.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kSurfaces = 4;
constexpr size_t kMaxRes = 16;

struct Rec { struct wl_client *owner; uint32_t id; };
std::mutex g_mu;
std::vector<Rec> g_recs;
std::atomic<bool> g_over{false};
std::atomic<bool> g_stop{false};
struct wl_surface_interface g_surf_impl {};
struct wl_compositor_interface g_comp_impl {};

void surf_destroy(struct wl_client *, struct wl_resource *r) { wl_resource_destroy(r); }

void comp_create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_recs.size() >= kMaxRes) {
    g_over = true;
    wl_resource_post_error(r, 0, "resource bound exceeded");
    return;
  }
  struct wl_resource *s = wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(r), id);
  if (!s) { wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(s, &g_surf_impl, nullptr, nullptr);
  g_recs.push_back({c, wl_resource_get_id(s)});
}

void comp_create_region(struct wl_client *, struct wl_resource *r, uint32_t) {
  wl_resource_post_error(r, 0, "region unsupported in test");
}

void bind_comp(struct wl_client *c, void *, uint32_t ver, uint32_t id) {
  struct wl_resource *r = wl_resource_create(c, &wl_compositor_interface, static_cast<int>(ver), id);
  if (!r) { wl_client_post_no_memory(c); return; }
  wl_resource_set_implementation(r, &g_comp_impl, nullptr, nullptr);
}

void on_alarm(int) {
  static const char msg[] = "{\"ok\":false,\"timeout\":true,\"fcitxLoaded\":false,"
                            "\"nativeToplevels\":false}\n";
  ssize_t n = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
  (void)n;
  _exit(3);
}

struct Conn {
  struct wl_display *d = nullptr;
  struct wl_registry *reg = nullptr;
  struct wl_registry *extra = nullptr;
  struct wl_compositor *comp = nullptr;
  std::vector<struct wl_surface *> surfs;
  std::vector<uint32_t> ids;
};

void reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t) {
  auto *c = static_cast<Conn *>(data);
  if (!c->comp && std::strcmp(iface, wl_compositor_interface.name) == 0)
    c->comp = static_cast<struct wl_compositor *>(wl_registry_bind(r, name, &wl_compositor_interface, 1));
}
void reg_remove(void *, struct wl_registry *, uint32_t) {}
const struct wl_registry_listener kRegListener = {reg_global, reg_remove};

bool setup_conn(Conn &c, bool shift) {
  if (!c.d) return false;
  if (shift && !(c.extra = wl_display_get_registry(c.d))) return false;  // shifts ID allocation
  if (shift && wl_registry_add_listener(c.extra, &kRegListener, &c) != 0) return false;
  if (!(c.reg = wl_display_get_registry(c.d))) return false;
  if (wl_registry_add_listener(c.reg, &kRegListener, &c) != 0) return false;
  if (wl_display_roundtrip(c.d) < 0 || !c.comp) return false;
  for (int i = 0; i < kSurfaces; ++i) {
    struct wl_surface *s = wl_compositor_create_surface(c.comp);
    if (!s) return false;
    c.surfs.push_back(s);
    c.ids.push_back(wl_proxy_get_id(reinterpret_cast<struct wl_proxy *>(s)));
  }
  return wl_display_roundtrip(c.d) >= 0 && wl_display_get_error(c.d) == 0;
}

void teardown_conn(Conn &c) {
  for (auto *s : c.surfs) wl_surface_destroy(s);
  if (c.comp) wl_compositor_destroy(c.comp);
  if (c.reg) wl_registry_destroy(c.reg);
  if (c.extra) wl_registry_destroy(c.extra);
  if (c.d) { wl_display_flush(c.d); wl_display_disconnect(c.d); }
  c = Conn{};
}
}  // namespace

int main() {
  std::signal(SIGALRM, on_alarm);
  alarm(6);
  g_surf_impl.destroy = surf_destroy;
  g_comp_impl.create_surface = comp_create_surface;
  g_comp_impl.create_region = comp_create_region;

  bool ok = true;
  struct wl_display *srv = wl_display_create();
  if (!srv || !wl_global_create(srv, &wl_compositor_interface, 1, nullptr, bind_comp)) ok = false;

  int fa[2] = {-1, -1}, fb[2] = {-1, -1};
  struct wl_client *ca = nullptr, *cb = nullptr;
  Conn A, B;
  if (ok && (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fa) != 0 ||
             socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fb) != 0)) ok = false;
  if (ok) {
    if (!(ca = wl_client_create(srv, fa[0]))) close(fa[0]);
    if (!(cb = wl_client_create(srv, fb[0]))) close(fb[0]);
    A.d = wl_display_connect_to_fd(fa[1]);
    B.d = wl_display_connect_to_fd(fb[1]);
    ok = ca && cb && A.d && B.d;
  } else {
    for (int fd : {fa[0], fa[1], fb[0], fb[1]}) if (fd >= 0) close(fd);
  }

  std::thread th;
  if (ok) {
    struct wl_event_loop *loop = wl_display_get_event_loop(srv);
    th = std::thread([srv, loop] {
      while (!g_stop.load()) {
        if (wl_event_loop_dispatch(loop, 20) < 0 && errno != EINTR) break;
        wl_display_flush_clients(srv);
      }
    });
    ok = setup_conn(A, false) && setup_conn(B, true);
  }

  std::vector<uint32_t> ida = A.ids, idb = B.ids;
  teardown_conn(A);
  teardown_conn(B);
  g_stop = true;
  if (th.joinable()) th.join();

  std::vector<uint32_t> sa, sb;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    for (const Rec &r : g_recs) {
      if (r.owner == ca) sa.push_back(r.id);
      else if (r.owner == cb) sb.push_back(r.id);
    }
  }
  if (srv) { wl_display_destroy_clients(srv); wl_display_destroy(srv); }
  alarm(0);

  int shared = 0, crossOrdinal = 0;
  for (size_t i = 0; i < ida.size(); ++i)
    for (size_t j = 0; j < idb.size(); ++j)
      if (ida[i] == idb[j]) { ++shared; if (i != j) ++crossOrdinal; }
  bool srvMatch = ok && sa == ida && sb == idb && ida.size() == size_t(kSurfaces);
  bool distinctOwners = ok && ca && cb && ca != cb;
  bool local = srvMatch && distinctOwners && crossOrdinal > 0;
  ok = ok && !g_over.load() && local;

  std::printf("{\"ok\":%s,\"scope\":\"core_surface_object_id_namespace_only\","
              "\"fcitxLoaded\":false,\"nativeToplevels\":false,\"surfacesPerClient\":%d,"
              "\"serverSurfaceRecords\":%zu,\"sharedNumericIds\":%d,\"sameIdDifferentOrdinal\":%d,"
              "\"serverIdsMatchClientIds\":%s,\"distinctOwningClients\":%s,"
              "\"idIsConnectionLocal\":%s,\"boundExceeded\":%s}\n",
              ok ? "true" : "false", kSurfaces, sa.size() + sb.size(), shared, crossOrdinal,
              srvMatch ? "true" : "false", distinctOwners ? "true" : "false",
              local ? "true" : "false", g_over.load() ? "true" : "false");
  return ok ? 0 : 1;
}
