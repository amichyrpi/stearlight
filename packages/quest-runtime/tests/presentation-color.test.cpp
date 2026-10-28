// Android/Quest hardware test: compile with the NDK and link EGL, GLESv3 and android.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <cstring>
#include "../app/src/main/cpp/presentation_shader.h"
GLuint shader(GLenum type,const char* text){auto s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);assert(ok);return s;}
int main(){
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(d,nullptr,nullptr));
 EGLint attr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
 EGLConfig cfg;EGLint n;assert(eglChooseConfig(d,attr,&cfg,1,&n)&&n);
 EGLint cattr[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},sattr[]={EGL_WIDTH,256,EGL_HEIGHT,1,EGL_NONE};
 auto c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,cattr);auto surf=eglCreatePbufferSurface(d,cfg,sattr);assert(eglMakeCurrent(d,surf,surf,c));
 AHardwareBuffer_Desc desc{};desc.width=512;desc.height=1;desc.layers=1;desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
 desc.usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
 AHardwareBuffer* ahb=nullptr;assert(!AHardwareBuffer_allocate(&desc,&ahb));AHardwareBuffer_describe(ahb,&desc);
 unsigned char* mem=nullptr;assert(!AHardwareBuffer_lock(ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY,-1,nullptr,(void**)&mem));
 for(int x=0;x<512;x++){int v=(x&255)^(x>=256?0x55:0);mem[x*4]=v;mem[x*4+1]=255-v;mem[x*4+2]=(v*31)%256;mem[x*4+3]=v;}
 assert(!AHardwareBuffer_unlock(ahb,nullptr));
 auto native=(PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
 auto image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
 auto target=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");assert(native&&image&&target);
 EGLint rawAttr[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
 EGLint srgbAttr[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_GL_COLORSPACE_KHR,EGL_GL_COLORSPACE_SRGB_KHR,EGL_NONE};
 EGLImageKHR images[]={image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(ahb),rawAttr),image(d,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,native(ahb),srgbAttr)};
 if(images[1]==EGL_NO_IMAGE_KHR){printf("Hardware sRGB image unavailable: EGL error %x\n",eglGetError());return 3;}
 GLuint textures[2];glGenTextures(2,textures);
 for(int i=0;i<2;i++){assert(images[i]!=EGL_NO_IMAGE_KHR);glBindTexture(GL_TEXTURE_2D,textures[i]);target(GL_TEXTURE_2D,images[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
 auto vs=shader(GL_VERTEX_SHADER,quest::presentationVertex);
 auto fs=shader(GL_FRAGMENT_SHADER,quest::presentationFragment);
 auto p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);glUseProgram(p);glViewport(0,0,256,1);
 unsigned char pixels[2][1024];int error=0;
 for(int format:{37,43,44,50})for(int eye=0;eye<2;eye++)for(int alpha=0;alpha<2;alpha++){
  bool encoded=format==43||format==50;
  glUniform1i(glGetUniformLocation(p,"eye"),eye);
  glUniform1i(glGetUniformLocation(p,"format"),format);
  glUniform1i(glGetUniformLocation(p,"preserveAlpha"),alpha);
  for(int i=0;i<2;i++){
   glBindTexture(GL_TEXTURE_2D,textures[i&&encoded?1:0]);
   glUniform1i(glGetUniformLocation(p,"decode"),encoded&&!i);
   glDrawArrays(GL_TRIANGLES,0,3);glFinish();
   glReadPixels(0,0,256,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[i]);assert(glGetError()==GL_NO_ERROR);
  }
  for(int x=0;x<256;x++)for(int channel=0;channel<4;channel++){
   int v=x^(eye?0x55:0);int values[]={v,255-v,(v*31)%256,v};
   if(format==44||format==50){int swap=values[0];values[0]=values[2];values[2]=swap;}
   double expected=values[channel]/255.;
   if(channel<3&&encoded)expected=expected<=.04045?expected/12.92:std::pow((expected+.055)/1.055,2.4);
   if(channel==3&&!alpha)expected=1;
   int reference=int(std::lround(expected*255));
   for(int mode=0;mode<2;mode++){
    int actual=pixels[mode][x*4+channel],delta=std::abs(actual-reference);
    if(delta>error)error=delta;
    if(delta>1){printf("Mismatch format=%d eye=%d alpha=%d x=%d channel=%d mode=%d expected=%d actual=%d\n",format,eye,alpha,x,channel,mode,reference,actual);return 4;}
   }
  }
 }
 printf("Production presentation shader: both eyes, all four formats, alpha and hardware/fallback decode pass; max error=%d/255\n",error);
 eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(d,surf);eglDestroyContext(d,c);eglTerminate(d);
}
