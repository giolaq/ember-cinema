#!/usr/bin/env python3
"""Exercise the actual native APK on an attached Android/Fire TV device."""
import pathlib, re, subprocess, time
ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / 'artifacts'
OUT.mkdir(exist_ok=True)
PKG = 'tv.cinema.nativeapp'
def adb(*args):
    return subprocess.check_output(['adb', *args], text=True)
def key(n):
    adb('shell', 'input', 'keyevent', str(n)); time.sleep(.25)
def logs():
    return adb('logcat', '-d', '-s', 'Ember:I', '*:S')
def wait_for(pattern, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if re.search(pattern, logs()): return
        time.sleep(.3)
    raise AssertionError(f'Timeout: {pattern}\n{logs()}')
def shot(name):
    time.sleep(.25)
    (OUT / (name+'.png')).write_bytes(subprocess.check_output(['adb','exec-out','screencap','-p']))
def status():
    matches = re.findall(r'status position=(\d+) paused=(\d) controls=(\d)', logs())
    assert matches, logs()
    return tuple(map(int, matches[-1]))
TRACE=[]
def clear():
    if TRACE: TRACE.append(logs())
    else: TRACE.append('Ember runtime integration test\n')
    adb('logcat','-c')
adb('shell','am','force-stop',PKG)
clear()
adb('shell','am','start','-n',PKG+'/android.app.NativeActivity')
wait_for('renderer 1920x1080')
shot('home')
# Focus wraps in both directions, and survives details/back navigation.
key(21); wait_for('focus=3'); key(22); wait_for('focus=0')
for i in range(4):
    key(23); wait_for(f'screen=details movie={i}')
    if i == 0: shot('details')
    key(4); key(22)
key(23); key(23)
wait_for('playing duration=52209')
wait_for('video-frame movie=0')
time.sleep(2)
key(85) # physical play/pause works even with hidden controls
wait_for('paused=1')
time.sleep(1.3); p1=status()[0]
shot('player-controls')
time.sleep(5); p2,paused,visible=status()
assert paused==1 and abs(p2-p1)<350, (p1,p2,paused)
assert visible==0, status()
shot('player-hidden')
# OK first reveals controls without accidentally resuming.
key(23);time.sleep(1.2)
assert status()[1:]==(1,1),status()
# Select the on-screen forward and backward controls.
key(22);key(23);time.sleep(1.5)
p3=status()[0];assert p3>p2+4000,(p2,p3)
key(21);key(21);key(23);time.sleep(1.5)
p4=status()[0];assert p4<p3-4000,(p3,p4)
# Resume using the actual on-screen Play button.
key(22);key(23);time.sleep(2.5)
assert status()[1]==0,status()
# Dedicated transport buttons also seek.
clear();key(90);wait_for('seek=');key(89)
assert len(re.findall('seek=',logs()))==2,logs()
# End-of-stream and replay.
for _ in range(7): key(90)
wait_for('ended',timeout=12)
shot('player-ended')
key(23);time.sleep(2)
assert status()[1]==0 and status()[0]<6000,status()
# Backgrounding releases playback, returning safely to details.
key(3);time.sleep(1)
adb('shell','am','start','-n',PKG+'/android.app.NativeActivity');time.sleep(1)
key(23);wait_for('playing duration=52209')
key(4);key(4);key(22);key(23);key(23)
wait_for('video=1280x720')
wait_for('video-frame movie=1')
time.sleep(2);shot('bunny-playing')
# Back while connecting must remain responsive and safely release the worker.
key(4);clear();key(23);key(4);time.sleep(2)
key(23);wait_for('playing duration=')
key(4);key(4);shot('home-bunny')
report='\n'.join(TRACE+[logs()])
(OUT/'runtime.log').write_text(report)
crashes=adb('logcat','-d','-b','crash')
assert PKG not in crashes,crashes
print('PASS: navigation, details, both HTTPS streams, pause, auto-hide, reveal, on-screen and remote seeking, replay, background/resume, loading cancellation.')
