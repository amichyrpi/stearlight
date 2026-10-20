// Optional argv[1]: privately extracted reference code from the verified Steam
// build. It is never bundled in the repository or CI artifacts.
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../linux/capture-matrix.h"
static uint32_t seed=0x451123;
static unsigned random_byte(void){seed=seed*1664525u+1013904223u;return seed>>24;}
static void reference_y(const uint8_t* src,uint8_t* dst,const uint8_t* m,int width){
 for(int x=0;x<width;x++)dst[x]=(m[0]*src[4*x+2]+m[1]*src[4*x+1]+m[2]*src[4*x]+(m[12]|m[13]<<8))>>8;
}
static void reference_uv(const uint8_t* src,int stride,uint8_t* u,uint8_t* v,const uint8_t* m,int width){
 for(int x=0;x<width;x+=2){
  int pixel[3];
  for(int c=0;c<3;c++){
   int left=(src[4*x+c]+src[stride+4*x+c]+1)/2;
   int right=x+1<width?(src[4*x+4+c]+src[stride+4*x+4+c]+1)/2:left;
   pixel[c]=(left+right+1)/2;
  }
  *u++=(m[4]*pixel[0]-m[5]*pixel[1]-m[6]*pixel[2]+(m[14]|m[15]<<8))>>8;
  *v++=(m[8]*pixel[2]-m[9]*pixel[1]-m[10]*pixel[0]+(m[14]|m[15]<<8))>>8;
 }
}
int main(int argc,char** argv){
 void(*original_y)(const uint8_t*,uint8_t*,const uint8_t*,int)=reference_y;
 void(*original_uv)(const uint8_t*,int,uint8_t*,uint8_t*,const uint8_t*,int)=reference_uv;
#if defined(__aarch64__)
 if(argc==2){
  FILE* f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=0x24fc&&size<65536);rewind(f);
  void* code=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(code!=MAP_FAILED);
  assert(fread(code,1,size,f)==(size_t)size);fclose(f);assert(!mprotect(code,size,PROT_READ|PROT_EXEC));
  __builtin___clear_cache(code,(char*)code+size);
  original_y=(void*)((char*)code+0x21a4);original_uv=(void*)((char*)code+0x2244);
 }
#else
 (void)argc;(void)argv;
#endif
 uint8_t src[4*260*2],matrix[16],y[260],expected_y[260],u[132],v[132],expected_u[132],expected_v[132];
 for(int trial=0;trial<10000;trial++){
  for(size_t i=0;i<sizeof(src);i++)src[i]=random_byte();
  for(size_t i=0;i<sizeof(matrix);i++)matrix[i]=random_byte();
  int width=1+trial%259,stride=4*260;
  const uint8_t* first=src;
  if(trial&1){first+=stride;stride=-stride;}
  memset(y,0xe3,sizeof(y));memset(expected_y,0xe3,sizeof(expected_y));
  memset(u,0xe3,sizeof(u));memset(expected_u,0xe3,sizeof(expected_u));
  memset(v,0xe3,sizeof(v));memset(expected_v,0xe3,sizeof(expected_v));
  original_y(first,expected_y,matrix,width);quest_matrix_y(first,y,matrix,width);
  original_uv(first,stride,expected_u,expected_v,matrix,width);quest_matrix_uv(first,stride,u,v,matrix,width);
  assert(!memcmp(y,expected_y,sizeof(y)));assert(!memcmp(u,expected_u,sizeof(u)));assert(!memcmp(v,expected_v,sizeof(v)));
 }
 puts("Capture matrix: 10000 color/width/stride cases match; output bounds preserved");
 return 0;
}
