// SPDX-License-Identifier: MIT
#include "wire-client.h"
#include "wire_common.h"
#include <wayland-client.h>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kMaxKeys = 4;

struct State {
  bool drain = false;
  wl_display *display = nullptr;
  uint32_t ctrlName = 0;
  bool haveCtrl = false;
  rasklad_wire_control_v1 *ctrl = nullptr;
  rasklad_wire_grab_v1 *oldGrab = nullptr;
  bool oldReleased = false;
  rasklad_wire_grab_v1 *newGrab = nullptr;
  wl_callback *sync = nullptr;
  bool retired = false;
  WireClientResult *res = nullptr;
};

void onGlobal(void *data, wl_registry *, uint32_t name, const char *iface, uint32_t) {
  auto *s = static_cast<State *>(data);
  if (!s->haveCtrl && std::strcmp(iface, rasklad_wire_control_v1_interface.name) == 0) {
    s->ctrlName = name;
    s->haveCtrl = true;
  }
}
void onGlobalRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener kRegistry = {onGlobal, onGlobalRemove};

void onKey(void *data, rasklad_wire_grab_v1 *, uint32_t state) {
  auto *s = static_cast<State *>(data);
  WireClientResult &r = *s->res;
  if (r.press + r.release >= kMaxKeys) { r.failed = 1; return; }
  if (state == 1) ++r.press;
  else if (state == 0) ++r.release;
  else r.failed = 1;
}
const rasklad_wire_grab_v1_listener kGrab = {onKey};

void onSyncDone(void *data, wl_callback *cb, uint32_t) {
  auto *s = static_cast<State *>(data);
  if(s->sync!=cb||!s->retired||!s->newGrab)s->res->failed=1;
  wl_callback_destroy(cb);
  s->sync = nullptr;
  if (s->drain && s->oldGrab) {
    wl_proxy_destroy(reinterpret_cast<wl_proxy *>(s->oldGrab));
    s->oldGrab = nullptr;
  }
  s->res->complete = 1;
}
const wl_callback_listener kSync = {onSyncDone};

void onRetire(void *data, rasklad_wire_control_v1 *) {
  auto *s = static_cast<State *>(data);
  if (s->retired || !s->oldGrab) { s->res->failed = 1; return; }
  s->retired = true;
  if (s->drain) {
    auto *p = reinterpret_cast<wl_proxy *>(s->oldGrab);
    wl_proxy_marshal_flags(p, RASKLAD_WIRE_GRAB_V1_RELEASE, nullptr, wl_proxy_get_version(p), 0);
    s->oldReleased = true;
  } else {
    rasklad_wire_grab_v1_release(s->oldGrab);
    s->oldGrab = nullptr;
  }
  s->newGrab = rasklad_wire_control_v1_get_grab(s->ctrl);
  if(!s->newGrab||rasklad_wire_grab_v1_add_listener(s->newGrab,&kGrab,s)!=0){s->res->failed=1;return;}
  s->sync = wl_display_sync(s->display);
  if(!s->sync||wl_callback_add_listener(s->sync,&kSync,s)!=0)s->res->failed=1;
}
const rasklad_wire_control_v1_listener kControl = {onRetire};

}  // namespace

int wire_client(int socketFd, bool drain, WireClientResult &result) {
  result = WireClientResult{};
  State s;
  s.drain = drain;
  s.res = &result;
  wl_display *dpy = wl_display_connect_to_fd(socketFd);
  if (!dpy) { result.failed = 1; return 1; }
  s.display=dpy;
  wl_registry *reg = wl_display_get_registry(dpy);
  if(!reg){result.failed=1;wl_display_disconnect(dpy);return 1;}
  if(wl_registry_add_listener(reg,&kRegistry,&s)!=0){result.failed=1;wl_registry_destroy(reg);wl_display_disconnect(dpy);return 1;}
  if (wl_display_roundtrip(dpy) < 0 || !s.haveCtrl) {
    result.failed = 1;
  } else {
    s.ctrl = static_cast<rasklad_wire_control_v1 *>(
        wl_registry_bind(reg, s.ctrlName, &rasklad_wire_control_v1_interface, 1));
    if(!s.ctrl||rasklad_wire_control_v1_add_listener(s.ctrl,&kControl,&s)!=0)result.failed=1;
    if(!result.failed)s.oldGrab = rasklad_wire_control_v1_get_grab(s.ctrl);
    if(!s.oldGrab||rasklad_wire_grab_v1_add_listener(s.oldGrab,&kGrab,&s)!=0)result.failed=1;
    if (!result.failed&&wl_display_roundtrip(dpy) < 0) result.failed = 1;
  }
  if (!result.failed) {
    rasklad_wire_control_v1_begin(s.ctrl);
    if (wl_display_flush(dpy) < 0) result.failed = 1;
    while (!result.complete && !result.failed)
      if (wl_display_dispatch(dpy) < 0) break;
  }
  result.protocolError = static_cast<uint32_t>(wl_display_get_error(dpy));
  if (s.sync) wl_callback_destroy(s.sync);
  if (s.oldGrab) {
    if (s.oldReleased) wl_proxy_destroy(reinterpret_cast<wl_proxy *>(s.oldGrab));
    else rasklad_wire_grab_v1_release(s.oldGrab);
  }
  if (s.newGrab) rasklad_wire_grab_v1_release(s.newGrab);
  if (s.ctrl) rasklad_wire_control_v1_destroy(s.ctrl);
  wl_registry_destroy(reg);
  wl_display_flush(dpy);
  wl_display_disconnect(dpy);
  if (!result.complete || result.failed || result.protocolError) {
    if (!result.complete) result.failed = 1;
    return 1;
  }
  return 0;
}
