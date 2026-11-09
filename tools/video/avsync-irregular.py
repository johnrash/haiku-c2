# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# avsync-irregular.py <recording> <crop w:h:x:y>: for a clip with flashes and
# beeps at irregular times (README): the one offset within +-10 s that pairs
# the most flashes with beeps, so whole seconds of offset show too.
import subprocess, sys, tempfile, os
import numpy as np
video, crop = sys.argv[1:3]
d = tempfile.mkdtemp()
subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-i",video,"-vf","crop=%s,scale=64:36,format=gray"%crop,"-f","rawvideo",d+"/v.raw"],check=True)
subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-i",video,"-vn","-ac","1","-ar","48000","-f","s16le",d+"/a.raw"],check=True)
st={}
for line in subprocess.run(["ffprobe","-v","error","-show_entries","stream=codec_type,start_time","-of","csv=p=0",video],capture_output=True,text=True).stdout.split():
    k,v=line.split(","); st[k]=float(v)
v=np.fromfile(d+"/v.raw",np.uint8).reshape(-1,36*64); b=v.mean(axis=1)
fl=[st['video']+i/60.0 for i in range(1,len(b)) if b[i]>200 and b[i-1]<=200]
a=np.fromfile(d+"/a.raw",np.int16).astype(float); win=240
rms=np.sqrt(np.convolve(a*a,np.ones(win)/win,mode='valid')[::win]); lv=rms>max(2000,rms.max()*0.3)
be=[st['audio']+i*win/48000.0 for i in range(1,len(lv)) if lv[i] and not lv[i-1]]
best=None
for dms in range(-10000,10001,5):
    dd=dms/1000.0
    n=sum(1 for f in fl if any(abs(x-f-dd)<0.04 for x in be))
    if best is None or n>best[0]: best=(n,dd)
n,dd=best
offs=[min((x-f for x in be),key=lambda y:abs(y-dd)) for f in fl]
offs=[o for o in offs if abs(o-dd)<0.04]
print("%s: %d flashes, %d beeps; best offset %+d ms matches %d (sound after picture); matched offsets %+d..%+d ms"%(os.path.basename(video),len(fl),len(be),dd*1000,n,min(offs)*1000 if offs else 0,max(offs)*1000 if offs else 0))
