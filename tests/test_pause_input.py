#!/usr/bin/env python3
"""Compile the actual app input handler and replay Momentum event sequences."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'app.c').read_text()
state = source[source.index('typedef struct {'):source.index('static bool read_slot')]
handler = source[source.index('static int keybit'):source.index('int32_t celeste_app')]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "progress.h"
typedef void FuriMutex;
typedef void FuriMessageQueue;
typedef void Storage;
typedef void ViewPort;
typedef enum {InputKeyUp,InputKeyDown,InputKeyRight,InputKeyLeft,InputKeyOk,InputKeyBack} InputKey;
typedef enum {InputTypePress,InputTypeRelease,InputTypeShort,InputTypeLong,InputTypeRepeat} InputType;
typedef struct { InputKey key; InputType type; } InputEvent;
static int render_music=3, music=-99, restarts=0;
static void audio_music(int value) { music=value; }
static void audio_set_enabled(bool value) { (void)value; }
static bool usb_link_start(void) { return true; }
static void usb_link_stop(void) {}
static void Celeste_Flipper_restart(void) { restarts++; }
'''
tests = r'''
static void send(App* a, InputKey key, InputType type) { event(a,(InputEvent){key,type}); }
static void tap(App* a, InputKey key) {
  send(a,key,InputTypePress); send(a,key,InputTypeRelease); send(a,key,InputTypeShort);
}
int main(void) {
  App a={0}; progress_default(&a.progress); a.running=true;
  send(&a,InputKeyBack,InputTypePress);
  assert(a.held==32 && a.pressed==32 && !a.menu); /* Dash remains immediate. */
  send(&a,InputKeyBack,InputTypeLong);
  assert(a.menu && !a.held && !a.pressed && music==-1);
  for(int i=0;i<20;i++) send(&a,InputKeyBack,InputTypeRepeat);
  assert(a.menu && music==-1);
  send(&a,InputKeyBack,InputTypeRelease); assert(a.menu);
  tap(&a,InputKeyBack); assert(!a.menu && music==render_music && !a.held && !a.pressed);
  tap(&a,InputKeyOk); assert(a.pressed==16); /* Jump remains immediate. */
  a.pressed=0;
  send(&a,InputKeyBack,InputTypePress); send(&a,InputKeyBack,InputTypeLong);
  send(&a,InputKeyBack,InputTypeRelease);
  tap(&a,InputKeyOk); assert(a.open_menu);
  send(&a,InputKeyBack,InputTypeLong); send(&a,InputKeyBack,InputTypeRepeat);
  assert(a.open_menu && a.menu);
  tap(&a,InputKeyBack); assert(!a.open_menu && a.menu);
  send(&a,InputKeyDown,InputTypeRepeat); assert(a.selected==1);
  send(&a,InputKeyOk,InputTypeRepeat); assert(a.menu);
  tap(&a,InputKeyOk); assert(!a.menu);
  a.menu=true; a.stats=true;
  send(&a,InputKeyBack,InputTypeRepeat); assert(a.stats && a.menu);
  tap(&a,InputKeyBack); assert(!a.stats && a.menu);
  a.confirm_restart=true;
  send(&a,InputKeyBack,InputTypeRepeat); assert(a.confirm_restart && !restarts);
  tap(&a,InputKeyBack); assert(!a.confirm_restart && a.menu && !restarts);
  a.confirm_restart=true; tap(&a,InputKeyOk); assert(restarts==1 && !a.menu);
  puts("PASS: actual C handler preserves dash/jump, pauses through long/repeat/release, resumes on fresh short Back, and handles submenus/restart.");
}
'''
with tempfile.TemporaryDirectory(prefix='celeste-input-') as folder:
    c = Path(folder)/'input.c'; executable = Path(folder)/'input-test'
    c.write_text(preamble + state + 'static bool save(App *a) {(void)a; return true;}\n' + handler + tests)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root),str(c),str(root/'progress.c'),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
