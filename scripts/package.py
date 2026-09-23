#!/usr/bin/env python3
"""Package only task-owned artifacts into the exact SD layout."""
import pathlib, shutil, hashlib, zipfile
r=pathlib.Path(__file__).resolve().parents[1];d=r/'dist';bundle=d/'Celeste-Flipper'
if bundle.exists():shutil.rmtree(bundle)
for rel in ('sdcard/apps/Games','sdcard/apps_data/celeste_classic','manual'):(bundle/rel).mkdir(parents=True,exist_ok=True)
shutil.copy2(d/'celeste_classic.fap',bundle/'sdcard/apps/Games/celeste_classic.fap')
shutil.copy2(d/'portable/Celeste-Windows.exe',bundle/'sdcard/apps_data/celeste_classic/Celeste-Windows.exe')
for f in sorted((d/'portable').glob('Celeste-*')):
 if f.name!='Celeste-Windows.exe':shutil.copy2(f,bundle/'manual'/f.name)
for name in ('README.md','THIRD_PARTY.md'):shutil.copy2(r/name,bundle/name)
files=[p for p in bundle.rglob('*') if p.is_file() and p.name!='SHA256SUMS']
(bundle/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(bundle))+'\n' for p in sorted(files)))
archive=d/'celeste-flipper-v0.1.1.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for p in sorted(bundle.rglob('*')):
  if p.is_file():z.write(p,p.relative_to(d))
fap=(d/'celeste_classic.fap').stat().st_size;win=(d/'portable/Celeste-Windows.exe').stat().st_size
total=sum(p.stat().st_size for p in bundle.rglob('*') if p.is_file())
text=f'Flipper FAP: {fap:,} bytes\nFlipper + Windows helper on SD: {fap+win:,} bytes\nComplete extracted bundle: {total:,} bytes\nRelease ZIP: {archive.stat().st_size:,} bytes\nOptional manual USB disk image: {(d/"celeste_portable.img").stat().st_size:,} bytes\n'
(d/'SIZES.txt').write_text(text);print(text)
