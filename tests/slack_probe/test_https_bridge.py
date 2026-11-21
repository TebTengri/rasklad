# SPDX-License-Identifier: MIT
import importlib.util, socket, tempfile, unittest, ssl
from pathlib import Path
p=Path(__file__).resolve().parents[2]/'tools/slack_https_bridge.py'
s=importlib.util.spec_from_file_location('https_bridge',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def hello(name):
 incoming,outgoing=ssl.MemoryBIO(),ssl.MemoryBIO()
 context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
 client=context.wrap_bio(incoming,outgoing,server_side=False,server_hostname=name)
 try:client.do_handshake()
 except ssl.SSLWantReadError:pass
 return outgoing.read()
class HttpsBridgePolicy(unittest.TestCase):
 def test_only_slack_https_connect(self):
  for host in ('slack.com','app.slack.com','a.slack-edge.com'):
   self.assertEqual(m.destination(('CONNECT '+host+':443 HTTP/1.1\r\n').encode()),host)
  for request in (b'GET https://slack.com/ HTTP/1.1',b'CONNECT slack.com:80 HTTP/1.1',
   b'CONNECT slack.com.evil:443 HTTP/1.1',b'CONNECT localhost:443 HTTP/1.1',
   b'CONNECT 127.0.0.1:443 HTTP/1.1',b'CONNECT user@slack.com:443 HTTP/1.1',
   b'CONNECT a..slack.com:443 HTTP/1.1',b'x'*8193):
   with self.subTest(request=request[:45]),self.assertRaises((ValueError,UnicodeError)):m.destination(request)
 def test_dns_rebinding_and_private_addresses_denied(self):
  def answer(ip):return lambda *a,**kw:[(socket.AF_INET,socket.SOCK_STREAM,6,'',(ip,443))]
  for ip in ('127.0.0.1','10.1.2.3','192.168.1.2','169.254.169.254','224.0.0.1','::1','::ffff:127.0.0.1'):
   with self.subTest(ip=ip),self.assertRaises(ValueError):m.public_addresses('app.slack.com',answer(ip))
  self.assertEqual(m.public_addresses('app.slack.com',answer('1.1.1.1'))[0][-1],('1.1.1.1',443))
 def test_real_client_hello_names_and_malformed_frames(self):
  class Stream:
   def __init__(self,data):self.data=data
   def recv(self,size):value=self.data[:size];self.data=self.data[size:];return value
  record=hello('app.slack.com')
  wire,name=m.tls_hello(Stream(record))
  self.assertEqual(name,'app.slack.com');self.assertEqual(wire,record)
  self.assertEqual(m.tls_hello(Stream(hello('other.example')))[1],'other.example')
  for invalid in (b'GET / HTTP/1.1',record[:12],b'\x16\x03\x03\xff\xff'):
   with self.assertRaises(ValueError):m.tls_hello(Stream(invalid))
 def test_wrong_sni_blocks_before_dns_or_connect(self):
  from unittest.mock import patch
  with tempfile.TemporaryDirectory() as directory:
   broker=m.Broker(Path(directory)/'proxy',online=True)
   with patch.object(m,'resolve_ipv4',side_effect=AssertionError('No DNS')) as resolver:
    try:
     with socket.socket(socket.AF_UNIX) as client:
      client.settimeout(2);client.connect(broker.path)
      client.sendall(b'CONNECT app.slack.com:443 HTTP/1.1\r\n\r\n')
      self.assertTrue(client.recv(256).startswith(b'HTTP/1.1 200'))
      client.sendall(hello('a.slack-edge.com'))
      self.assertEqual(client.recv(256),b'')
    finally:stats=broker.close()
    resolver.assert_not_called()
   self.assertEqual(stats['tlsDenied'],1);self.assertEqual(stats['opened'],0)
   self.assertTrue(stats['cleanupComplete'])
 def test_encrypted_or_missing_server_name_rejected(self):
  name=b'app.slack.com';names=b'\0'+len(name).to_bytes(2,'big')+name
  value=len(names).to_bytes(2,'big')+names
  ext=b'\0\0'+len(value).to_bytes(2,'big')+value
  def packet(extensions):
   body=b'\x03\x03'+bytes(32)+b'\0\0\2\x13\x01\1\0'+len(extensions).to_bytes(2,'big')+extensions
   return b'\1'+len(body).to_bytes(3,'big')+body
  self.assertEqual(m.client_name(packet(ext)),'app.slack.com')
  self.assertEqual(m.client_name(packet(ext+b'\xfe\x0d\0\0'),allow_ech=True),'app.slack.com')
  with self.assertRaises(ValueError):m.client_name(packet(b'\xfe\x0d\0\0'),allow_ech=True)
  for extensions in (b'',ext+ext,ext+b'\xfe\x0d\0\0'):
   with self.assertRaises(ValueError):m.client_name(packet(extensions))
 def test_offline_broker_denies_without_dns(self):
  with tempfile.TemporaryDirectory() as directory:
   broker=m.Broker(Path(directory)/'proxy',online=False)
   from unittest.mock import patch
   spy=patch.object(m,'resolve_ipv4',side_effect=AssertionError('DNS must not run'))
   resolver=spy.start()
   try:
    with socket.socket(socket.AF_UNIX) as client:
     client.settimeout(2);client.connect(broker.path)
     client.sendall(b'CONNECT app.slack.com:443 HTTP/1.1\r\n\r\n')
     self.assertTrue(client.recv(256).startswith(b'HTTP/1.1 403'))
   finally:stats=broker.close();spy.stop()
   resolver.assert_not_called()
   self.assertEqual(stats['opened'],0);self.assertEqual(stats['blocked'],1)
if __name__=='__main__':unittest.main()
