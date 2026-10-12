#!/usr/bin/env python3
"""Launch a Pebble QEMU emulator headlessly, install a PBW, screenshot it.

Usage: emu_run.py <platform> <pbw|-> <out-prefix> <proto_port> <cons_port> <gdb_port> <mon_port>
"""
import subprocess, socket, sys, os, time, signal
sys.path.insert(0, '/root/ptvenv/lib/python3.12/site-packages')

QEMU  = '/root/.local/share/pebble-sdk/SDKs/4.33.1/toolchain/bin/qemu-pebble'
SDK   = '/root/.local/share/pebble-sdk/SDKs/4.33.1/sdk-core'

CFG = {
  'emery':  dict(machine='pebble-emery',   cpu='cortex-m33', flash='mtd',  audio=True,  w=200, h=228),
  'flint':  dict(machine='pebble-flint',   cpu='cortex-m4',  flash='mtd',  audio=True,  w=144, h=168),
  'gabbro': dict(machine='pebble-gabbro',  cpu='cortex-m33', flash='mtd',  audio=True,  w=260, h=260),
  'basalt': dict(machine='pebble-snowy-bb',cpu='cortex-m4',  flash='none', audio=False,  w=144, h=168),
}

def main():
    plat, pbw, prefix, proto, cons, gdb, mon = sys.argv[1:8]
    proto, cons, gdb, mon = map(int, (proto, cons, gdb, mon))
    c = CFG[plat]
    micro = f'{SDK}/pebble/{plat}/qemu/qemu_micro_flash.bin'
    flash = f'/tmp/qw/{plat}_spi.bin'
    assert os.path.exists(micro), micro
    assert os.path.exists(flash), flash
    log = open(f'{prefix}.qemu.log', 'wb')

    cmd = [QEMU, '-rtc', 'base=localtime', '-serial', 'null',
           '-serial', f'tcp::{proto},server=on,wait=off',
           '-serial', f'tcp::{cons},server=on,wait=off',
           '-kernel', micro,
           '-gdb', f'tcp::{gdb},server=on,wait=off',
           '-monitor', f'tcp::{mon},server=on,wait=off',
           '-machine', c['machine'], '-cpu', c['cpu']]
    if c['flash'] == 'mtd':
        cmd += ['-drive', f'if=mtd,format=raw,file={flash}']
    else:
        cmd += ['-drive', f'if=none,id=spi-flash,file={flash},format=raw']
    if c['audio']:
        cmd += ['-audio', 'driver=sdl,id=audio0']
    cmd += ['-display', 'sdl,show-cursor=on']

    print('LAUNCH:', ' '.join(cmd), flush=True)
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    print('qemu pid =', proc.pid, flush=True)
    try:
        run(plat, pbw, prefix, proto, c, proc)
    finally:
        try: os.kill(proc.pid, signal.SIGKILL)
        except Exception: pass
        time.sleep(0.5)
        print('killed qemu', proc.pid, flush=True)

def run(plat, pbw, prefix, proto, c, proc):
    from libpebble2.communication import PebbleConnection
    from libpebble2.communication.transports.qemu import QemuTransport
    from libpebble2.services.screenshot import Screenshot
    from libpebble2.services.install import AppInstaller
    from PIL import Image

    conn = None
    for attempt in range(40):
        if proc.poll() is not None:
            print('!! QEMU EXITED early, rc=', proc.poll(), flush=True)
            print(open(f'{prefix}.qemu.log', errors='replace').read()[:3000])
            return
        time.sleep(1.5)
        try:
            t = QemuTransport('127.0.0.1', proto)
            p = PebbleConnection(t)
            p.connect()
            p.run_async()
            conn = p
            print(f'booted after ~{attempt*1.5:.0f}s; fw={p.firmware_version} platform={p.watch_platform}', flush=True)
            break
        except Exception as e:
            if attempt % 5 == 0:
                print(f'  waiting... attempt {attempt}: {type(e).__name__}: {e}', flush=True)
            continue
    if conn is None:
        print('!! never booted / no protocol response', flush=True)
        print(open(f'{prefix}.qemu.log', errors='replace').read()[:3000])
        return

    _orig = PebbleConnection.send_packet
    def throttled(self, packet):
        time.sleep(0.002)
        return _orig(self, packet)
    PebbleConnection.send_packet = throttled

    def shot(path, label):
        rows = Screenshot(conn).grab_image()
        h = len(rows); w = len(rows[0]) // 3
        img = Image.new('RGB', (w, h))
        px = img.load()
        for y, row in enumerate(rows):
            for x in range(w):
                px[x, y] = (row[x*3], row[x*3+1], row[x*3+2])
        img.save(path)
        from collections import Counter
        cc = Counter(list(img.getdata()))
        print(f'[{label}] {path} {w}x{h} colors={len(cc)} top={cc.most_common(4)}', flush=True)
        return img

    shot(f'{prefix}-before.png', 'boot-screen')
    if pbw != '-':
        print('installing', pbw, os.path.getsize(pbw), 'bytes', flush=True)
        t0 = time.time()
        AppInstaller(conn, pbw).install(force_install=True)
        print(f'install OK in {time.time()-t0:.1f}s', flush=True)
        time.sleep(5)
        shot(f'{prefix}-after.png', 'watchface')
        time.sleep(4)
        shot(f'{prefix}-after2.png', 'watchface+4s')
    print('DONE', plat, flush=True)

main()
