#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Private workflow sound rotation; never downloads or distributes audio."""
import argparse,fcntl,json,os,stat,subprocess
from pathlib import Path

STOCK=Path('/usr/share/sounds/freedesktop/stereo/complete.oga')
def sound_fd(path,local=True):
    fd=None
    try:
        fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
        info=os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or not 0<info.st_size<=524288
            or info.st_uid not in ((os.getuid(),) if local else (0,os.getuid()))):
            os.close(fd);return None
        return fd
    except OSError:
        if fd is not None:
            try:os.close(fd)
            except OSError:pass
        return None

def invoke(fd):
    return subprocess.run(['/usr/bin/pw-play','--volume=0.5',f'/proc/self/fd/{fd}'],
        pass_fds=(fd,),stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,timeout=4).returncode==0

def play(directory,player=invoke):
    opened=[];lock=None
    try:
        safe=False
        try:
            info=directory.lstat()
            safe=stat.S_ISDIR(info.st_mode) and info.st_uid==os.getuid() and not info.st_mode&0o022
        except OSError:pass
        pool=[]
        if safe:
            for n in (1,2):
                fd=sound_fd(directory/f'owner-alert-sound-{n}.mp3')
                if fd is not None:pool.append((n,fd));opened.append(fd)
        if len(pool)==2:
            lock=os.open(directory/'owner-alert-sequence',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
            info=os.fstat(lock)
            if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o022 or info.st_nlink!=1 or info.st_size>16:return None
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:return None
            index=1 if os.read(lock,16).strip()==b'2' else 0
            number,fd=pool[index]
            if not player(fd):return None
            os.lseek(lock,0,os.SEEK_SET);os.write(lock,b'1\n' if index else b'2\n');os.ftruncate(lock,2)
            return number
        if pool:return pool[0][0] if player(pool[0][1]) else None
        fd=sound_fd(STOCK,False)
        if fd is None:return None
        opened.append(fd)
        return 0 if player(fd) else None
    finally:
        if lock is not None:os.close(lock)
        for fd in opened:os.close(fd)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--play',action='store_true');args=p.parse_args()
    if not args.play:print('Plan only: --play rotates two owner-local ignored files.');return 0
    try:
        number=play(Path(__file__).resolve().parents[1]/'build')
        print(json.dumps({'played':number is not None,'variant':number}));return 0 if number is not None else 1
    except (OSError,subprocess.TimeoutExpired):return 1
if __name__=='__main__':raise SystemExit(main())
