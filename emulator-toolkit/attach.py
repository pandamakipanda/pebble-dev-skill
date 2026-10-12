#!/usr/bin/env python3
"""Attach to an already-running Pebble QEMU, install PBW, screenshot.
Usage: attach.py <proto_port> <pbw|-> <out-prefix>"""
import sys, os, time
sys.path.insert(0, '/root/ptvenv/lib/python3.12/site-packages')
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu import QemuTransport
from libpebble2.services.screenshot import Screenshot
from libpebble2.services.install import AppInstaller
from PIL import Image
from collections import Counter

PORT, PBW, PREFIX = int(sys.argv[1]), sys.argv[2], sys.argv[3]
p = PebbleConnection(QemuTransport('127.0.0.1', PORT))
p.connect(); p.run_async()
print(f'connected: platform={p.watch_platform} fw={p.firmware_version}', flush=True)
_orig = PebbleConnection.send_packet
def throttled(self, packet):
    time.sleep(0.002); return _orig(self, packet)
PebbleConnection.send_packet = throttled

def shot(path, label):
    rows = Screenshot(p).grab_image()
    h = len(rows); w = len(rows[0]) // 3
    img = Image.new('RGB', (w, h)); px = img.load()
    for y, row in enumerate(rows):
        for x in range(w): px[x, y] = (row[x*3], row[x*3+1], row[x*3+2])
    img.save(path)
    cc = Counter(list(img.getdata()))
    print(f'[{label}] {path} {w}x{h} colors={len(cc)} top={cc.most_common(4)}', flush=True)

shot(f'{PREFIX}-before.png', 'boot-screen')
if PBW != '-':
    print('installing', PBW, os.path.getsize(PBW), 'bytes', flush=True)
    t0 = time.time()
    AppInstaller(p, PBW).install(force_install=True)
    print(f'install OK in {time.time()-t0:.1f}s', flush=True)
    time.sleep(6)
    shot(f'{PREFIX}-after.png', 'watchface')
    time.sleep(4)
    shot(f'{PREFIX}-after2.png', 'watchface+4s')
print('DONE', flush=True)
