# SPDX-License-Identifier: MIT
"""Bounded CONNECT tunnel: fixed Slack domains, public destinations, no TLS interception."""
import ipaddress, re, select, socket, threading, time, subprocess
DOMAINS=('slack.com','slack-edge.com','slack-imgs.com')
MAX_HEADER=8192
HELLO_WAIT_SECONDS=20  # bounded by max_active and the broker deadline

def destination(header):
    if len(header)>MAX_HEADER:raise ValueError('header')
    line=header.split(b'\r\n',1)[0].decode('ascii')
    method,authority,version=line.split(' ')
    if method!='CONNECT' or version not in ('HTTP/1.0','HTTP/1.1'):raise ValueError('method')
    host,port=authority.rsplit(':',1);host=host.lower()
    if port!='443' or len(host)>253 or not re.fullmatch(r'[a-z0-9.-]+',host):raise ValueError('authority')
    if any(not label or len(label)>63 or label.startswith('-') or label.endswith('-') for label in host.split('.')):raise ValueError('name')
    if not any(host==d or host.endswith('.'+d) for d in DOMAINS):raise ValueError('domain')
    return host

# Rejected targets are reported by suffix ONLY when it is public infrastructure;
# anything else (company SSO, customer domains) collapses to 'other'.
KNOWN_SUFFIXES=('slack-files.com','slackb.com','slack-core.com','slack-msgs.com','slack-redir.net','slackhq.com',
    'gvt1.com','gvt2.com','google.com','googleapis.com','gstatic.com','sentry.io','amazonaws.com','cloudfront.net',
    'giphy.com','gravatar.com','okta.com','microsoftonline.com')

def rejected_suffix(header):
    # Last two labels of a CONNECT target, reported only if in KNOWN_SUFFIXES.
    try:
        authority=header.split(b'\r\n',1)[0].split(b' ')[1].decode('ascii').lower()
        host=authority.rsplit(':',1)[0]
    except (IndexError,UnicodeError):return 'malformed'
    if len(host)>253 or not re.fullmatch(r'[a-z0-9.-]+',host):return 'malformed'
    labels=[label for label in host.split('.') if label]
    if len(labels)<2:return 'malformed'
    suffix='.'.join(labels[-2:])
    return suffix if suffix in KNOWN_SUFFIXES else 'other'

def public_addresses(host,resolver=socket.getaddrinfo):
    answers=resolver(host,443,type=socket.SOCK_STREAM)
    result=[]
    for family,kind,proto,_,address in answers[:32]:
        ip=ipaddress.ip_address(address[0])
        if not ip.is_global or ip.is_multicast or ip.is_reserved:continue
        entry=(family,kind,proto,address)
        if entry not in result:result.append(entry)
    if not result:raise ValueError('nonpublic-dns')
    return result

def client_name(hello,allow_ech=False):
    # RFC8446 ClientHello / RFC6066 server_name. This parses routing metadata,
    # not cryptography. ECH is denied by default; an explicit test policy may
    # permit it while leaving the inner hostname unverified.
    if len(hello)<38 or hello[0]!=1 or int.from_bytes(hello[1:4],'big')!=len(hello)-4:raise ValueError('hello')
    body=hello[4:];at=34
    def vector(width):
        nonlocal at
        if at+width>len(body):raise ValueError('vector')
        size=int.from_bytes(body[at:at+width],'big');at+=width
        if at+size>len(body):raise ValueError('vector')
        value=body[at:at+size];at+=size;return value
    vector(1);ciphers=vector(2);compression=vector(1);extensions=vector(2)
    if at!=len(body) or not ciphers or len(ciphers)%2 or not compression:raise ValueError('hello')
    at=0;names=[];seen=set()
    while at<len(extensions):
        if at+4>len(extensions):raise ValueError('extension')
        kind=int.from_bytes(extensions[at:at+2],'big');size=int.from_bytes(extensions[at+2:at+4],'big');at+=4
        if kind in seen or at+size>len(extensions):raise ValueError('extension')
        seen.add(kind);value=extensions[at:at+size];at+=size
        if kind==0xfe0d and not allow_ech:raise ValueError('encrypted-name')
        if kind==0:
            if len(value)<5 or int.from_bytes(value[:2],'big')!=len(value)-2:raise ValueError('name')
            if value[2]!=0 or int.from_bytes(value[3:5],'big')!=len(value)-5:raise ValueError('name')
            names.append(value[5:].decode('ascii').lower())
    if len(names)!=1:raise ValueError('name')
    return names[0]

def tls_hello(client,initial=b'',allow_ech=False):
    pending=bytearray(initial);wire=bytearray();handshake=bytearray()
    def take(count):
        while len(pending)<count:
            chunk=client.recv(min(16384,count-len(pending)))
            if not chunk:raise ValueError('tls-eof')
            pending.extend(chunk)
        result=bytes(pending[:count]);del pending[:count];return result
    for unused in range(4):
        header=take(5);size=int.from_bytes(header[3:5],'big')
        if header[0]!=22 or header[1]!=3 or header[2] not in (1,2,3) or not 0<size<=16384:raise ValueError('tls-record')
        record=take(size);wire.extend(header+record);handshake.extend(record)
        if len(handshake)>=4:
            needed=4+int.from_bytes(handshake[1:4],'big')
            if needed>16388:raise ValueError('hello-size')
            if len(handshake)>=needed:return bytes(wire)+bytes(pending),client_name(bytes(handshake[:needed]),allow_ech)
    raise ValueError('tls-fragments')

def error_kind(exc,stage):
    # Fixed categories only; no hostnames, addresses or payloads.
    if isinstance(exc,socket.timeout):kind='timeout'
    elif isinstance(exc,(ConnectionResetError,BrokenPipeError,ConnectionAbortedError)):kind='reset'
    elif type(exc) is OSError and str(exc) in ('dns','connect'):kind=str(exc)
    elif isinstance(exc,subprocess.TimeoutExpired):kind='dns-timeout'
    elif isinstance(exc,OSError):kind='os'
    else:kind={'IndexError':'index','KeyError':'key','TypeError':'type','AttributeError':'attribute'}.get(type(exc).__name__,'other')
    return stage+':'+kind

def resolve_ipv4(host,port,**unused):
    # Bounded resolver subprocess; no shell, environment secrets or payload logs.
    result=subprocess.run(['/usr/bin/getent','ahostsv4',host],stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,timeout=2,env={'PATH':'/usr/bin','LANG':'C'})
    if result.returncode or len(result.stdout)>32768:raise OSError('dns')
    addresses={line.split()[0].decode('ascii') for line in result.stdout.splitlines() if line.split()}
    return [(socket.AF_INET,socket.SOCK_STREAM,socket.IPPROTO_TCP,'',(ip,port)) for ip in sorted(addresses)]

def pump(left,right,deadline,allow_bytes):
    while time.monotonic()<deadline:
        ready,_,_=select.select([left,right],[],[],min(1,max(0,deadline-time.monotonic())))
        for source in ready:
            data=source.recv(16384)
            if not data or not allow_bytes(len(data)):return
            (right if source is left else left).sendall(data)

class Broker:
    def __init__(self,path,online=False,allow_ech=False,seconds=30,max_requests=64,max_bytes=64*1024*1024,max_active=8,accept_hello=False):
        # accept_hello: offline diagnostic. Answer 200, parse the ClientHello, record the outcome, close.
        # Never opens an outbound connection; incompatible with online.
        if accept_hello and online:raise ValueError('accept-hello-offline-only')
        self.accept_hello=accept_hello
        if not (1<=seconds<=120 and 1<=max_requests<=512 and 1<=max_bytes<=512*1024*1024 and 1<=max_active<=32):raise ValueError('broker-bounds')
        self.path=str(path);self.online=online;self.allow_ech=allow_ech;self.deadline=time.monotonic()+seconds
        self.max_requests=max_requests;self.max_bytes=max_bytes
        self.lock=threading.Lock();self.active=threading.BoundedSemaphore(max_active)
        self.stats={'accepted':0,'blocked':0,'tlsDenied':0,'tlsRejectReasons':{},'rejectedDomains':{},'opened':0,'errors':0,'errorKinds':{},'helloParsed':0,'forwardedBytes':0}
        self.listener=socket.socket(socket.AF_UNIX);self.listener.bind(self.path)
        import os
        os.chmod(self.path,0o600);self.listener.listen(8);self.listener.settimeout(.2)
        self.stopped=False;self.sockets=set();self.workers=[]
        self.acceptor=threading.Thread(target=self._serve,daemon=True);self.acceptor.start()
    def _serve(self):
        while not self.stopped and time.monotonic()<self.deadline:
            try:client,_=self.listener.accept()
            except socket.timeout:continue
            except OSError:return
            with self.lock:
                self.stats['accepted']+=1;admitted=self.stats['accepted']<=self.max_requests
            if not admitted:client.close();return
            if not self.active.acquire(False):client.close();continue
            worker=threading.Thread(target=self._client,args=(client,),daemon=True)
            self.workers.append(worker);worker.start()
    def _allow(self,count):
        with self.lock:
            if self.stats['forwardedBytes']+count>self.max_bytes:return False
            self.stats['forwardedBytes']+=count;return True
    def _client(self,client):
        remote=None;established=False;stage='header'
        with self.lock:
            if self.stopped:client.close();self.active.release();return
            self.sockets.add(client)
        try:
            client.settimeout(3);header=b''
            while b'\r\n\r\n' not in header:
                chunk=client.recv(min(2048,MAX_HEADER+1-len(header)))
                if not chunk or len(header)+len(chunk)>MAX_HEADER:raise ValueError('header')
                header+=chunk
            head,tail=header.split(b'\r\n\r\n',1)
            try:host=destination(head)
            except ValueError as exc:
                if str(exc)=='domain':
                    suffix=rejected_suffix(head)
                    with self.lock:
                        counts=self.stats['rejectedDomains']
                        if suffix in counts or len(counts)<32:counts[suffix]=counts.get(suffix,0)+1
                        else:counts['other']=counts.get('other',0)+1
                raise
            if not self.online and not self.accept_hello:raise ValueError('offline-check')
            client.sendall(b'HTTP/1.1 200 Connection Established\r\n\r\n');established=True;stage='hello'
            # Chromium may preconnect a tunnel and send its ClientHello only when a request needs it.
            client.settimeout(HELLO_WAIT_SECONDS)
            try:
                initial,name=tls_hello(client,tail,self.allow_ech)
                if name!=host:raise ValueError('sni-mismatch')
                if self.accept_hello:
                    with self.lock:self.stats['helloParsed']+=1
                    return
            except (ValueError,UnicodeError) as exc:
                reasons={'hello','vector','extension','name','encrypted-name','tls-eof','tls-record','hello-size','tls-fragments','sni-mismatch'}
                reason=str(exc) if type(exc) is ValueError and str(exc) in reasons else 'malformed'
                with self.lock:
                    self.stats['tlsDenied']+=1
                    counts=self.stats['tlsRejectReasons'];counts[reason]=counts.get(reason,0)+1
                raise
            # Resolve once, reject private/loopback results, connect to pinned IP.
            stage='resolve'
            if self.stopped or time.monotonic()>=self.deadline:return
            answers=public_addresses(host,resolve_ipv4)
            for family,kind,proto,address in answers:
                if self.stopped or time.monotonic()>=self.deadline:return
                candidate=socket.socket(family,kind,proto);candidate.settimeout(3)
                with self.lock:
                    if self.stopped:candidate.close();return
                    self.sockets.add(candidate)
                try:candidate.connect(address);remote=candidate;break
                except OSError:
                    with self.lock:self.sockets.discard(candidate)
                    candidate.close()
            stage='connect'
            if remote is None:raise OSError('connect')
            with self.lock:self.sockets.add(remote);self.stats['opened']+=1
            if not self._allow(len(initial)):return
            stage='pump'
            remote.sendall(initial)
            pump(client,remote,self.deadline,self._allow)
        except (ValueError,UnicodeError):
            with self.lock:self.stats['blocked']+=1
            try:
                if not established:client.sendall(b'HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n')
            except OSError:pass
        except Exception as exc:
            kind=error_kind(exc,stage)
            with self.lock:
                self.stats['errors']+=1
                kinds=self.stats['errorKinds'];kinds[kind]=kinds.get(kind,0)+1
        finally:
            for peer in (client,remote):
                if peer:
                    with self.lock:self.sockets.discard(peer)
                    peer.close()
            self.active.release()
    def close(self):
        self.stopped=True;self.listener.close();self.acceptor.join(.5)
        with self.lock:
            for peer in list(self.sockets):
                try:peer.shutdown(socket.SHUT_RDWR)
                except OSError:pass
                peer.close()
        end=time.monotonic()+3
        for worker in list(self.workers):worker.join(max(0,end-time.monotonic()))
        with self.lock:return dict(self.stats,cleanupComplete=not self.acceptor.is_alive() and not any(w.is_alive() for w in self.workers))

class Relay:
    """Private namespace TCP proxy forwards only to our mounted Unix broker."""
    def __init__(self,path,seconds=30,max_active=8):
        if not (1<=seconds<=120 and 1<=max_active<=32):raise ValueError('relay-bounds')
        self.path=path;self.deadline=time.monotonic()+seconds;self.active=threading.BoundedSemaphore(max_active)
        self.refused=0
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(8)
        self.listener.settimeout(.2);self.port=self.listener.getsockname()[1];self.stopped=False
        self.lock=threading.Lock();self.sockets=set();self.workers=[]
        self.acceptor=threading.Thread(target=self._serve,daemon=True);self.acceptor.start()
    def _serve(self):
        while not self.stopped and time.monotonic()<self.deadline:
            try:client,_=self.listener.accept()
            except socket.timeout:continue
            except OSError:return
            if not self.active.acquire(False):
                with self.lock:self.refused+=1
                client.close();continue
            worker=threading.Thread(target=self._client,args=(client,),daemon=True)
            self.workers.append(worker);worker.start()
    def _client(self,client):
        remote=socket.socket(socket.AF_UNIX)
        with self.lock:
            if self.stopped:client.close();remote.close();self.active.release();return
            self.sockets.update((client,remote))
        try:
            client.settimeout(3);remote.settimeout(3);remote.connect(self.path)
            pump(client,remote,self.deadline,lambda count:True)
        except OSError:pass
        finally:
            with self.lock:self.sockets.discard(client);self.sockets.discard(remote)
            client.close();remote.close();self.active.release()
    def close(self):
        self.stopped=True;self.listener.close();self.acceptor.join(.5)
        with self.lock:
            for peer in list(self.sockets):
                try:peer.shutdown(socket.SHUT_RDWR)
                except OSError:pass
                peer.close()
        end=time.monotonic()+3
        for worker in list(self.workers):worker.join(max(0,end-time.monotonic()))
