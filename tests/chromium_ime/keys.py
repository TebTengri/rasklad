# SPDX-License-Identifier: MIT
"""Fixed synthetic key sequences, ONLY for this lab's private X server."""
import ctypes as c
import os
from pathlib import Path
import sys
import time

CASES={'word':'ghbdtn','space':' ','xspace':'x ','hello':'hello ','mixed':'1ghbdtn ','long':'a'*33+'ghbdtn '}
if (os.environ.get('RASKLAD_CHROMIUM_IME_LAB')!='1' or os.environ.get('DISPLAY')!=':77'
        or os.environ.get('HOME')!='/home/lab' or not Path('/lab/fixture.html').is_file()
        or os.environ.get('WAYLAND_DISPLAY')):raise SystemExit('not private lab')
if len(sys.argv)!=3 or sys.argv[1] not in {*CASES,'undo','backspace','escape'}:raise SystemExit('fixed cases only')
pid=int(sys.argv[2]);assert 1<pid<1_000_000
if os.readlink(f'/proc/{pid}/exe')!='/usr/lib/chromium/chromium':raise SystemExit('not owned browser')
x=c.CDLL('/usr/lib/libX11.so.6');t=c.CDLL('/usr/lib/libXtst.so.6')
D=c.c_void_p;W=c.c_ulong
class Attr(c.Structure):
 _fields_=[('x',c.c_int),('y',c.c_int),('width',c.c_int),('height',c.c_int),('border_width',c.c_int),('depth',c.c_int),('visual',D),('root',W),('winclass',c.c_int),('bit_gravity',c.c_int),('win_gravity',c.c_int),('backing_store',c.c_int),('backing_planes',W),('backing_pixel',W),('save_under',c.c_int),('colormap',W),('map_installed',c.c_int),('map_state',c.c_int),('all_event_masks',c.c_long),('your_event_mask',c.c_long),('do_not_propagate_mask',c.c_long),('override_redirect',c.c_int),('screen',D)]
x.XGetWindowAttributes.argtypes=[D,W,c.POINTER(Attr)]
x.XOpenDisplay.argtypes=[c.c_char_p];x.XOpenDisplay.restype=D
x.XDefaultRootWindow.argtypes=[D];x.XDefaultRootWindow.restype=W
x.XInternAtom.argtypes=[D,c.c_char_p,c.c_int];x.XInternAtom.restype=W
x.XQueryTree.argtypes=[D,W,c.POINTER(W),c.POINTER(W),c.POINTER(c.POINTER(W)),c.POINTER(c.c_uint)]
x.XGetWindowProperty.argtypes=[D,W,W,c.c_long,c.c_long,c.c_int,W,c.POINTER(W),c.POINTER(c.c_int),c.POINTER(W),c.POINTER(W),c.POINTER(c.POINTER(c.c_ubyte))]
x.XFree.argtypes=[D];x.XSetInputFocus.argtypes=[D,W,c.c_int,W]
x.XStringToKeysym.argtypes=[c.c_char_p];x.XStringToKeysym.restype=W
x.XKeysymToKeycode.argtypes=[D,W];x.XKeysymToKeycode.restype=c.c_uint
x.XSync.argtypes=[D,c.c_int];x.XCloseDisplay.argtypes=[D]
t.XTestFakeKeyEvent.argtypes=[D,c.c_uint,c.c_int,W]
d=x.XOpenDisplay(b':77')
if not d:raise SystemExit('private display unavailable')
try:
 root=x.XDefaultRootWindow(d);atom=x.XInternAtom(d,b'_NET_WM_PID',1);focus=None
 for _ in range(30):
  rr=W();pp=W();children=c.POINTER(W)();count=c.c_uint()
  if x.XQueryTree(d,root,c.byref(rr),c.byref(pp),c.byref(children),c.byref(count)):
   try:
    for i in range(count.value):
     typ=W();fmt=c.c_int();n=W();after=W();data=c.POINTER(c.c_ubyte)()
     rc=x.XGetWindowProperty(d,children[i],atom,0,1,0,0,c.byref(typ),c.byref(fmt),c.byref(n),c.byref(after),c.byref(data))
     try:
      if rc==0 and fmt.value==32 and n.value==1 and c.cast(data,c.POINTER(W))[0]==pid:
       attrs=Attr()
       if x.XGetWindowAttributes(d,children[i],c.byref(attrs)) and attrs.map_state==2 and attrs.width>=400 and attrs.height>=200:
        focus=children[i];break
     finally:
      if data:x.XFree(data)
   finally:
    if children:x.XFree(children)
  if focus:break
  time.sleep(.05)
 if not focus:raise SystemExit('owned browser window unavailable')
 x.XSetInputFocus(d,focus,2,0);x.XSync(d,0)
 def key(name,down):
  code=x.XKeysymToKeycode(d,x.XStringToKeysym(name.encode('ascii')))
  if not code:raise RuntimeError('unknown fixed keysym')
  if not t.XTestFakeKeyEvent(d,code,down,0):raise RuntimeError('private XTest failed')
  x.XSync(d,0);time.sleep(.025)
 case=sys.argv[1]
 if case=='undo':key('Control_L',1);key('z',1);key('z',0);key('Control_L',0)
 elif case in ('backspace','escape'):
  name={'backspace':'BackSpace','escape':'Escape'}[case];key(name,1);key(name,0)
 else:
  for ch in CASES[case]:
   name='space' if ch==' ' else ch
   key(name,1);key(name,0)
finally:x.XCloseDisplay(d)
