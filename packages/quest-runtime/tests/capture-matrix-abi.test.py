"""Unknown capture libraries must retain their original row implementation."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    (folder / 'original.c').write_text('''
#include <stdint.h>
void ARGBToYRowMatrix_C(const uint8_t* s,uint8_t* y,const uint8_t* m,int n){
 for(int i=0;i<n;i++)y[i]=s[i]^m[0];
}
void ARGBToUVRowMatrix_C(const uint8_t* s,int stride,uint8_t* u,uint8_t* v,const uint8_t* m,int n){
 for(int i=0;i<(n+1)/2;i++){u[i]=s[i]+m[1];v[i]=s[stride+i]+m[2];}
}
''')
    (folder / 'preload.c').write_text('''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>
static int quest_steam_capture(void){return 1;}
#define QUEST_CAPTURE_MATRIX_EXPORTS
#include "capture-matrix.h"
''')
    (folder / 'client.c').write_text('''
#include <stdint.h>
#include <assert.h>
void ARGBToYRowMatrix_C(const uint8_t*,uint8_t*,const uint8_t*,int);
void ARGBToUVRowMatrix_C(const uint8_t*,int,uint8_t*,uint8_t*,const uint8_t*,int);
int main(void){
 uint8_t src[64],matrix[16]={3,5,7},y[16],u[8],v[8];
 for(int i=0;i<64;i++)src[i]=i;
 for(int repeat=0;repeat<3;repeat++){
  ARGBToYRowMatrix_C(src,y,matrix,16);
  ARGBToUVRowMatrix_C(src,32,u,v,matrix,16);
  for(int i=0;i<16;i++)assert(y[i]==(uint8_t)(src[i]^3));
  for(int i=0;i<8;i++){assert(u[i]==(uint8_t)(src[i]+5));assert(v[i]==(uint8_t)(src[32+i]+7));}
 }
 return 0;
}
''')
    subprocess.run(['cc', '-shared', '-fPIC', folder / 'original.c', '-o', folder / 'liboriginal.so'], check=True)
    subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-I', ROOT / 'packages/quest-runtime/linux',
                    folder / 'preload.c', '-ldl', '-o', folder / 'preload.so'], check=True)
    subprocess.run(['cc', folder / 'client.c', '-L', folder, '-loriginal', '-Wl,-rpath,' + str(folder),
                    '-o', folder / 'steam'], check=True)
    subprocess.run([folder / 'steam'], env=dict(os.environ, LD_PRELOAD=str(folder / 'preload.so')), check=True)
print('Capture ABI: unknown Y/UV implementations forwarded unchanged')
