// Accelerate the verified Frame RGB-to-YUV row ABI, preserving its rounding.
// The shipped Matrix_C functions have no NEON path and are built without
// optimization. Other libyuv/Steam builds retain their own implementation.
#ifndef QUEST_CAPTURE_MATRIX_H
#define QUEST_CAPTURE_MATRIX_H
#include <stdint.h>
#include <stddef.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static inline unsigned quest_matrix_bias(const uint8_t* matrix,unsigned offset){
 return matrix[offset]|((unsigned)matrix[offset+1]<<8);
}
static inline void quest_matrix_y(const uint8_t* src,uint8_t* dst,const uint8_t* matrix,int width){
 unsigned r=matrix[0],g=matrix[1],b=matrix[2],bias=quest_matrix_bias(matrix,12);
#if defined(__aarch64__)
 for(;width>=8;width-=8,src+=32,dst+=8){
  uint8x8x4_t pixels=vld4_u8(src);
  uint16x8_t y=vdupq_n_u16(bias);
  y=vmlal_u8(y,pixels.val[2],vdup_n_u8(r));
  y=vmlal_u8(y,pixels.val[1],vdup_n_u8(g));
  y=vmlal_u8(y,pixels.val[0],vdup_n_u8(b));
  vst1_u8(dst,vshrn_n_u16(y,8));
 }
#endif
 for(int x=0;x<width;x++)dst[x]=(r*src[4*x+2]+g*src[4*x+1]+b*src[4*x]+bias)>>8;
}
static inline void quest_matrix_uv(const uint8_t* src,int stride,uint8_t* u,uint8_t* v,const uint8_t* matrix,int width){
 const uint8_t* next=src+stride;unsigned bias=quest_matrix_bias(matrix,14);
#if defined(__aarch64__)
 for(;width>=8;width-=8,src+=32,next+=32,u+=4,v+=4){
  uint8x8x4_t first=vld4_u8(src),second=vld4_u8(next);
  // Preserve the two rounded averages, rather than rounding a four-pixel sum.
  uint16x4_t b=vrshr_n_u16(vpaddl_u8(vrhadd_u8(first.val[0],second.val[0])),1);
  uint16x4_t g=vrshr_n_u16(vpaddl_u8(vrhadd_u8(first.val[1],second.val[1])),1);
  uint16x4_t r=vrshr_n_u16(vpaddl_u8(vrhadd_u8(first.val[2],second.val[2])),1);
  uint16x4_t uu=vdup_n_u16(bias),vv=uu;
  uu=vmla_n_u16(uu,b,matrix[4]);uu=vmls_n_u16(uu,g,matrix[5]);uu=vmls_n_u16(uu,r,matrix[6]);
  vv=vmla_n_u16(vv,r,matrix[8]);vv=vmls_n_u16(vv,g,matrix[9]);vv=vmls_n_u16(vv,b,matrix[10]);
  uint8x8_t packed_u=vshrn_n_u16(vcombine_u16(uu,vdup_n_u16(0)),8);
  uint8x8_t packed_v=vshrn_n_u16(vcombine_u16(vv,vdup_n_u16(0)),8);
  vst1_lane_u32((uint32_t*)u,vreinterpret_u32_u8(packed_u),0);
  vst1_lane_u32((uint32_t*)v,vreinterpret_u32_u8(packed_v),0);
 }
#endif
 for(int x=0;x<width;x+=2){
  unsigned values[3];
  for(int channel=0;channel<3;channel++){
   unsigned first=(src[4*x+channel]+next[4*x+channel]+1)>>1;
   if(x+1<width){unsigned second=(src[4*x+4+channel]+next[4*x+4+channel]+1)>>1;first=(first+second+1)>>1;}
   values[channel]=first;
  }
  *u++=(matrix[4]*values[0]-matrix[5]*values[1]-matrix[6]*values[2]+bias)>>8;
  *v++=(matrix[8]*values[2]-matrix[9]*values[1]-matrix[10]*values[0]+bias)>>8;
 }
}

#ifdef QUEST_CAPTURE_MATRIX_EXPORTS
#if defined(__aarch64__)
static uint64_t quest_matrix_fingerprint(const void* pointer,size_t count){
 const unsigned char* p=pointer;uint64_t hash=UINT64_C(14695981039346656037);
 for(size_t i=0;i<count;i++)hash=(hash^p[i])*UINT64_C(1099511628211);
 return hash;
}
static const void* quest_matrix_branch(const void* function,unsigned offset){
 const unsigned char* pc=(const unsigned char*)function+offset;uint32_t instruction;memcpy(&instruction,pc,4);
 int32_t displacement=(int32_t)(instruction<<6)>>4;
 return pc+displacement;
}
#endif
static void* quest_matrix_original(const char* name){
 void* result=dlsym(RTLD_NEXT,name);
 if(!result){
  void* library=dlopen("/opt/steam/steamrtarm64/steamui.so",RTLD_NOW|RTLD_NOLOAD);
  if(library){result=dlsym(library,name);dlclose(library);}
 }
 return result;
}
void ARGBToYRowMatrix_C(const uint8_t* src,uint8_t* dst,const uint8_t* matrix,int width){
 typedef void(*function)(const uint8_t*,uint8_t*,const uint8_t*,int);
 static _Atomic(function) original;static _Atomic int compatible;
 function real=atomic_load(&original);
 if(!real){real=(function)quest_matrix_original("ARGBToYRowMatrix_C");if(!real)return;atomic_store(&original,real);}
 int supported=atomic_load(&compatible);
 if(!supported){
  supported=-1;
#if defined(__aarch64__)
  if(quest_steam_capture()&&quest_matrix_fingerprint((const void*)real,160)==UINT64_C(0xb90ccd3d1ffee9ee)&&
     quest_matrix_fingerprint(quest_matrix_branch((const void*)real,0x4c),124)==UINT64_C(0xea051fd3f8f199fe))supported=1;
#endif
  atomic_store(&compatible,supported);
  if(supported==1)fprintf(stderr,"QUEST_CAPTURE_Y_NEON_READY\n");
 }
 if(supported==1)quest_matrix_y(src,dst,matrix,width);else real(src,dst,matrix,width);
}
void ARGBToUVRowMatrix_C(const uint8_t* src,int stride,uint8_t* u,uint8_t* v,const uint8_t* matrix,int width){
 typedef void(*function)(const uint8_t*,int,uint8_t*,uint8_t*,const uint8_t*,int);
 static _Atomic(function) original;static _Atomic int compatible;
 function real=atomic_load(&original);
 if(!real){real=(function)quest_matrix_original("ARGBToUVRowMatrix_C");if(!real)return;atomic_store(&original,real);}
 int supported=atomic_load(&compatible);
 if(!supported){
  supported=-1;
#if defined(__aarch64__)
  if(quest_steam_capture()&&quest_matrix_fingerprint((const void*)real,696)==UINT64_C(0xcef51186967288f7)&&
     quest_matrix_fingerprint(quest_matrix_branch((const void*)real,0x160),124)==UINT64_C(0x3cbcfecc0b0f2a62)&&
     quest_matrix_fingerprint(quest_matrix_branch((const void*)real,0x180),124)==UINT64_C(0x7ba8214609fdf662))supported=1;
#endif
  atomic_store(&compatible,supported);
  if(supported==1)fprintf(stderr,"QUEST_CAPTURE_UV_NEON_READY\n");
 }
 if(supported==1)quest_matrix_uv(src,stride,u,v,matrix,width);else real(src,stride,u,v,matrix,width);
}
#endif
#endif
