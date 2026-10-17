#!/usr/bin/env python3
"""Bundle the base rootfs; the APK installs the separate Quest overlay afterward."""
import argparse
import gzip
import hashlib
import os
from pathlib import Path, PurePosixPath
import tarfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--overlay',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=args.root.resolve();overlay=args.overlay.resolve();output=args.output.resolve()
    if not (root/'opt/steam/steamrtarm64/steam').is_file():
        raise SystemExit('Input must be the clean ARM64 Frame rootfs')
    output.parent.mkdir(parents=True,exist_ok=True)
    overrides=set()
    for directory,dirs,files in os.walk(overlay,followlinks=False):
        for name in dirs+files:overrides.add((Path(directory)/name).relative_to(overlay).as_posix())
    excluded={'boot','dev','proc','sys','run','tmp','var/cache','var/log','lib/modules','lib/firmware','lost+found'}
    count=0
    with output.open('wb') as raw,gzip.GzipFile(filename='',mode='wb',fileobj=raw,compresslevel=1,mtime=0) as compressed,tarfile.open(fileobj=compressed,mode='w|',format=tarfile.PAX_FORMAT) as archive:
        def add(base,path):
            nonlocal count
            relative=path.relative_to(base).as_posix();info=archive.gettarinfo(str(path),relative)
            if not (info.isfile() or info.isdir() or info.issym() or info.islnk()):return
            info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0;info.pax_headers={}
            if info.issym() and info.linkname.startswith('/'):
                info.linkname=os.path.relpath(info.linkname.lstrip('/'),str(PurePosixPath(relative).parent))
            if info.isfile():
                with path.open('rb') as source:archive.addfile(info,source)
            else:archive.addfile(info)
            count+=1
        # Overlay files are separate APK assets. Including them here as well
        # duplicates KDE, Chromium and the GPU runtime, exceeding APK limits.
        for base in [root]:
            for directory,dirs,files in os.walk(base,followlinks=False):
                dirs.sort();files.sort()
                for name in dirs+files:
                    path=Path(directory)/name;relative=path.relative_to(base).as_posix()
                    if base==root and (relative in overrides or any(relative==x or relative.startswith(x+'/') for x in excluded)):continue
                    add(base,path)
                if base==root:dirs[:]=[name for name in dirs if (Path(directory)/name).relative_to(root).as_posix() not in excluded]
        for directory in ['dev','dev/shm','proc','sys','run','run/stearlight','run/stearlight/quest','tmp']:
            info=tarfile.TarInfo(directory);info.type=tarfile.DIRTYPE;info.mode=0o755;archive.addfile(info)
    digest=hashlib.file_digest(output.open('rb'),'sha256').hexdigest()
    output.with_suffix('.sha256').write_text(digest+'\n')
    print(f'Rootfs: {count} entries, {output.stat().st_size} bytes, sha256 {digest}',flush=True)

if __name__=='__main__':main()
