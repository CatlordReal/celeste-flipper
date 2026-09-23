#!/usr/bin/env python3
"""Build portable launchers, then pin the Windows payload hash for the FAP."""
import argparse, hashlib, os, pathlib, shutil, subprocess, urllib.request
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--windows-only',action='store_true');args=p.parse_args()
native=root/'portable/native';out=root/'dist/portable';out.mkdir(parents=True,exist_ok=True)
archive=native/'ccleste-win.zip';expected='73f07ddf101fb8274e3341f701bde62a83477b695cff42b172b48680995f1ec7'
if not archive.exists():urllib.request.urlretrieve('https://github.com/lemon32767/ccleste/releases/download/v1.4.0/ccleste-win.zip',archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==expected,'Upstream archive hash mismatch'
shutil.copyfile(root/'portable/celeste.html',native/'celeste.html')
targets=[('windows','amd64','Celeste-Windows.exe','0')]
if not args.windows_only:targets += [('linux','amd64','Celeste-Linux-amd64','0'),('darwin','arm64','Celeste-Mac-arm64','1'),('darwin','amd64','Celeste-Mac-amd64','1')]
for system,arch,name,cgo in targets:
 env=dict(os.environ,GOOS=system,GOARCH=arch,CGO_ENABLED=cgo)
 if system=='darwin':env['CC']='clang -arch '+('x86_64' if arch=='amd64' else 'arm64')
 flags='-s -w'+(' -H=windowsgui' if system=='windows' else '')
 subprocess.run(['go','build','-trimpath','-ldflags='+flags,'-o',str(out/name),'.'],cwd=native,env=env,check=True)
h=hashlib.sha256((out/'Celeste-Windows.exe').read_bytes()).hexdigest()
(root/'windows_payload.h').write_text('#pragma once\n#define CELESTE_WINDOWS_SHA256 "'+h+'"\n')
print('Windows helper SHA-256:',h)
