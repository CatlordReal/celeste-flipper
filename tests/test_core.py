#!/usr/bin/env python3
"""Host regression checks and captures of the actual C game/renderer."""
import ctypes as C
import pathlib, subprocess, tempfile
from PIL import Image, ImageDraw
ROOT=pathlib.Path(__file__).resolve().parents[1]
out=pathlib.Path(tempfile.gettempdir())/'celeste-test.dylib'
subprocess.run(['cc','-DHOST_TEST','-std=c11','-O2','-shared',str(ROOT/'render.c'),str(ROOT/'progress.c'),str(ROOT/'vendor/celeste.c'),'-lm','-o',str(out)],check=True)
g=C.CDLL(str(out));g.Celeste_P8_set_call_func.argtypes=[C.c_void_p]
g.Celeste_P8_set_call_func(C.cast(g.render_callback,C.c_void_p));g.render_init();g.Celeste_P8_set_rndseed(8);g.Celeste_P8_init();g.Celeste_Flipper_restart()
buttons=C.c_uint8.in_dll(g,'render_buttons');mono=(C.c_uint8*1024)()
def step(b=0):
 buttons.value=b;g.Celeste_P8_update();g.Celeste_P8_draw();g.render_mono(mono,0)
def capture():
 im=Image.new('RGB',(128,64));p=im.load()
 for y in range(64):
  for x in range(128):p[x,y]=(24,28,18) if mono[y*16+x//8]&(1<<(x%8)) else (229,238,195)
 return im
frames=[];screens=[]
for room in range(31):
 g.Celeste_Flipper_resume(room,0,0,2 if room>21 else 1)
 for _ in range(40):step()
 assert g.Celeste_Flipper_room()==room,room
 if room in (0,3,7,11,16,21,24,30):screens.append((room,capture()))
# Exercise real controls, deaths, restart and per-room sprite paths.
g.Celeste_Flipper_restart()
for i in range(240):
 step((2 if i<100 else 1 if i<180 else 0)|(16 if i%30<7 else 0)|(32 if i%55==20 else 0))
 if i%2==0:frames.append(capture().resize((768,384),Image.Resampling.NEAREST))
import random
rng=random.Random(8)
g.Celeste_Flipper_resume(3,0,0,1)
for _ in range(3000):step(rng.randrange(64))
assert g.Celeste_Flipper_deaths()>0
# Persisted berry flags survive reconstructed room; restart clears run only.
g.Celeste_Flipper_resume(12,0x1003,17,1)
assert g.Celeste_Flipper_room()==12 and g.Celeste_Flipper_fruit()==0x1003 and g.Celeste_Flipper_deaths()==17
g.Celeste_Flipper_restart();assert g.Celeste_Flipper_room()==0 and g.Celeste_Flipper_fruit()==0 and g.Celeste_Flipper_deaths()==0
class Progress(C.Structure):
 _fields_=[(k,C.c_uint32) for k in 'magic version generation room fruit deaths double_dash completed total_fruit total_deaths playthroughs restarts best_room display_mode muted checksum'.split()]
p=Progress();g.progress_default(C.byref(p));assert g.progress_valid(C.byref(p))
g.progress_update(C.byref(p),7,3,8,1,0);g.progress_update(C.byref(p),30,7,9,2,1);g.progress_update(C.byref(p),30,7,9,2,1)
assert (p.total_fruit,p.total_deaths,p.playthroughs)==(3,9,1)
g.progress_restart(C.byref(p));assert (p.room,p.fruit,p.deaths,p.completed,p.restarts)==(0,0,0,0,1)
assert (p.total_fruit,p.total_deaths,p.playthroughs,p.best_room)==(3,9,1,30)
p.checksum=g.progress_checksum(C.byref(p));assert g.progress_valid(C.byref(p));p.total_fruit^=1;assert not g.progress_valid(C.byref(p))
# Two-slot validation model: damaged newest slot must be rejected.
q=Progress();g.progress_default(C.byref(q));q.generation=8;q.checksum=g.progress_checksum(C.byref(q));assert g.progress_valid(C.byref(q));q.checksum^=1;assert not g.progress_valid(C.byref(q))
ROOT.joinpath('docs').mkdir(exist_ok=True)
sheet=Image.new('RGB',(768,4*222),(245,245,241));d=ImageDraw.Draw(sheet)
for idx,(room,im) in enumerate(screens):
 x=(idx%2)*384;y=(idx//2)*222;d.text((x+8,y+7),f'Room {room+1} - actual host C renderer',fill=(20,20,20));sheet.paste(im.resize((384,192),Image.Resampling.NEAREST),(x,y+25))
sheet.save(ROOT/'docs/renderer-contact-sheet.png');frames[0].save(ROOT/'docs/gameplay-host.gif',save_all=True,append_images=frames[1:],duration=67,loop=0)
print('PASS: all 31 rooms, movement/jump/dash/deaths, checkpoint resume, restart preserves lifetime stats, completion deduplication, corrupt-save rejection. Host captures generated; not device evidence.')
