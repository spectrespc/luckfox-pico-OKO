import serial, time, glob

LOG = '/tmp/kilo/netboot-run4.log'
DEADLINE = time.time() + 900
f = open(LOG, 'wb', buffering=0)
f.write(b'[run2 start %s]\n' % time.strftime('%H:%M:%S').encode())

s = None
tail = b''
state = 'spam'
last_spam = 0.0
last_rx = time.time()

CMDS = [
    'setenv ipaddr 192.168.52.150',
    'setenv netmask 255.255.255.0',
    'setenv serverip 192.168.52.200',
    'setenv gatewayip 192.168.52.254',
    "setenv sys_bootargs 'root=/dev/nfs rw nfsroot=192.168.52.200:/srv/nfs/rootfs,vers=3,proto=tcp,nolock ip=192.168.52.150:192.168.52.200:192.168.52.254:255.255.255.0:luckfox:eth0:off rk_dma_heap_cma=66M'",
    'printenv sys_bootargs',
    'ping 192.168.52.200',
    'tftp ${ramdisk_addr_r} boot.img',
    'boot_fit ${ramdisk_addr_r}',
]

def log(b):
    global tail
    f.write(b)
    tail = (tail + b)[-65536:]

while time.time() < DEADLINE:
    if s is None:
        ports = sorted(glob.glob('/dev/ttyACM*'))
        if not ports:
            time.sleep(0.1); continue
        try:
            s = serial.Serial(ports[-1], 115200, timeout=0)
            log(b'\n[opened %s state=%s at %s]\n' % (ports[-1].encode(), state.encode(), time.strftime('%H:%M:%S').encode()))
        except Exception as e:
            log(b'[open-fail %s]\n' % str(e).encode()); s = None; time.sleep(0.2); continue

    if state == 'spam':
        now = time.time()
        if now - last_spam >= 0.015:
            try:
                s.write(b'\x03'); last_spam = now
            except Exception as e:
                log(b'\n[write-error %s]\n' % str(e).encode())
                try: s.close()
                except Exception: pass
                s = None; continue
        try:
            c = s.read(16384)
        except Exception as e:
            log(b'\n[read-error %s]\n' % str(e).encode())
            try: s.close()
            except Exception: pass
            s = None; continue
        if c:
            log(c)
            if b'=>' in tail[-8192:]:
                state = 'flush'
                last_rx = time.time()
                log(b'\n[DETECTED PROMPT at %s, stop spam, flush]\n' % time.strftime('%H:%M:%S').encode())

    elif state == 'flush':
        try:
            c = s.read(16384)
        except Exception as e:
            log(b'\n[read-error %s]\n' % str(e).encode())
            try: s.close()
            except Exception: pass
            s = None; continue
        if c:
            log(c); last_rx = time.time()
        else:
            if time.time() - last_rx > 2.5:
                state = 'command'
                log(b'\n[FLUSH DONE at %s]\n' % time.strftime('%H:%M:%S').encode())
            else:
                time.sleep(0.05)

    elif state == 'command':
        s.write(b'\r'); time.sleep(0.8)
        try:
            c = s.read(16384)
            if c: log(c)
        except Exception: pass
        log(b'\n[SENDING COMMANDS]\n')
        for cmd in CMDS:
            log(b'\n>>> ' + cmd.encode() + b'\n')
            try:
                s.write(cmd.encode() + b'\r')
            except Exception as e:
                log(b'[write-error %s]\n' % str(e).encode()); break
            t0 = time.time()
            while time.time() - t0 < (8.0 if cmd.startswith('tftp') else 5.0):
                try:
                    c = s.read(16384)
                except Exception as e:
                    log(b'\n[read-error %s]\n' % str(e).encode()); break
                if c: log(c)
                else: time.sleep(0.03)
        state = 'boot'
        log(b'\n[COMMANDS SENT at %s]\n' % time.strftime('%H:%M:%S').encode())

    else:
        try:
            c = s.read(16384)
            if c: log(c)
            else: time.sleep(0.05)
        except Exception as e:
            log(b'\n[read-error %s]\n' % str(e).encode())
            try: s.close()
            except Exception: pass
            s = None

log(b'\n[run2 end %s state=%s]\n' % (time.strftime('%H:%M:%S').encode(), state.encode()))
f.close()