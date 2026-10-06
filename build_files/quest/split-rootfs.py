#!/usr/bin/env python3
"""Split the rootfs stream into APK assets small enough for Android build tools."""
import argparse
import hashlib
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    parser.add_argument('assets',type=Path)
    args=parser.parse_args()
    directory=args.assets.resolve();parts=directory/'rootfs';parts.mkdir(parents=True,exist_ok=True)
    names=[];digest=hashlib.sha256()
    with args.archive.open('rb') as source:
        while data:=source.read(16*1024*1024):
            name=f'rootfs/part-{len(names):03d}.bin'
            (directory/name).write_bytes(data);names.append(name);digest.update(data)
    if not names:raise SystemExit('Rootfs archive is empty')
    (directory/'rootfs.list').write_text('\n'.join(names)+'\n')
    (directory/'rootfs.sha256').write_text(digest.hexdigest()+'\n')
    print(f'Bundled {len(names)} rootfs parts; sha256 {digest.hexdigest()}',flush=True)

if __name__=='__main__':main()
