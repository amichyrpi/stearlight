#!/usr/bin/env python3
"""Build the pinned PRoot with the Quest ARM64 robust-mutex adapter."""
import argparse,os,shutil,subprocess,tempfile,zipfile,importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('quest_patch',Path(__file__).with_name('patch-proot.py'))
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
def build(archive,prefix,ndk,output):
 toolchain=Path(ndk)/'toolchains/llvm/prebuilt/linux-x86_64/bin'
 compiler=toolchain/'aarch64-linux-android29-clang'
 if not compiler.is_file():raise RuntimeError(f'Linux Android NDK required: {compiler}')
 output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='quest-proot-',dir=output.parent) as directory:
  with zipfile.ZipFile(archive) as bundle:bundle.extractall(directory)
  source=Path(directory)/'proot-5.1.107.96';patch.patch(source)
  subprocess.run(['make','-f','GNUmakefile','-j'+str(min(os.cpu_count() or 2,8)),
   'CC='+str(compiler),'OBJCOPY='+str(toolchain/'llvm-objcopy'),'OBJDUMP='+str(toolchain/'llvm-objdump'),
   'CPPFLAGS=-I'+str(Path(prefix)/'include')+' -I. -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64',
   'LDFLAGS=-L'+str(Path(prefix)/'lib')+' -ltalloc -Wl,-z,noexecstack',
   'PROOT_UNBUNDLE_LOADER=/unused'],cwd=source/'src',check=True)
  shutil.copyfile(source/'src/proot',output)
  subprocess.run(['patchelf','--replace-needed','libtalloc.so.2','libtalloc.so','--set-rpath','$ORIGIN',output],check=True)
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--archive',required=True,type=Path);parser.add_argument('--prefix',required=True,type=Path);parser.add_argument('--android-ndk',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
 args=parser.parse_args();build(args.archive.resolve(),args.prefix.resolve(),args.android_ndk.resolve(),args.output.resolve())
