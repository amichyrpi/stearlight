#include "gpu_bridge.h"
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <vector>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"StearlightGPU",__VA_ARGS__)
static GLuint shader(GLenum type,const char* text){GLuint s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024];glGetShaderInfoLog(s,sizeof(log),nullptr,log);LOG("%s",log);throw std::runtime_error("GPU bridge shader");}return s;}
// Obtain the shared allocation through the public NDK socket transfer API.
// Linux imports the DMA-BUF rather than depending on Android gralloc structs.
static int dmaBuf(AHardwareBuffer* buffer){int sockets[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets))throw std::runtime_error("AHB transfer socket");if(AHardwareBuffer_sendHandleToUnixSocket(buffer,sockets[0])){close(sockets[0]);close(sockets[1]);throw std::runtime_error("AHB handle transfer");}char payload[65536],control[CMSG_SPACE(32*sizeof(int))]{};iovec io{payload,sizeof(payload)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);int result=-1;if(recvmsg(sockets[1],&message,MSG_CMSG_CLOEXEC)>0)for(cmsghdr* header=CMSG_FIRSTHDR(&message);header;header=CMSG_NXTHDR(&message,header))if(header->cmsg_level==SOL_SOCKET&&header->cmsg_type==SCM_RIGHTS){size_t count=(header->cmsg_len-CMSG_LEN(0))/sizeof(int);auto fds=reinterpret_cast<int*>(CMSG_DATA(header));for(size_t i=0;i<count;i++){struct stat data{};if(result<0&&!fstat(fds[i],&data)&&data.st_size>0)result=fds[i];else close(fds[i]);}}close(sockets[0]);close(sockets[1]);if(result<0)throw std::runtime_error("AHB transfer contained no allocation FD");return result;}
void GpuBridge::initialize(EGLDisplay d,const std::string& directory,uint32_t width,uint32_t height){
 display=d;path=directory+"/gpu.sock";hello.magic=QUEST_GPU_MAGIC;hello.version=QUEST_GPU_VERSION;hello.count=QUEST_GPU_BUFFERS;
 auto getClient=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));auto createImage=reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));auto targetImage=reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
 if(!getClient||!createImage||!targetImage)throw std::runtime_error("AHardwareBuffer EGL extensions");
 for(int i=0;i<QUEST_GPU_BUFFERS;i++){
  AHardwareBuffer_Desc desc{};desc.width=width;desc.height=height;desc.layers=1;desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;desc.usage=AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_CPU_READ_RARELY;
  if(AHardwareBuffer_allocate(&desc,&buffers[i]))throw std::runtime_error("AHardwareBuffer_allocate");AHardwareBuffer_describe(buffers[i],&desc);dmaFds[i]=dmaBuf(buffers[i]);struct stat stat{};if(fstat(dmaFds[i],&stat))throw std::runtime_error("DMA-BUF size");
  hello.buffers[i]={desc.width,desc.height,desc.stride,desc.format,static_cast<uint64_t>(stat.st_size)};
  EGLint attr[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};images[i]=createImage(display,EGL_NO_CONTEXT,EGL_NATIVE_BUFFER_ANDROID,getClient(buffers[i]),attr);if(images[i]==EGL_NO_IMAGE_KHR)throw std::runtime_error("AHardwareBuffer EGL image");glGenTextures(1,&textures[i]);glBindTexture(GL_TEXTURE_2D,textures[i]);targetImage(GL_TEXTURE_2D,images[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);LOG("DMA-BUF %d: %ux%u stride=%u size=%llu",i,desc.width,desc.height,desc.stride,(unsigned long long)stat.st_size);
 }
 glGenTextures(1,&history);glBindTexture(GL_TEXTURE_2D,history);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,width,height);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glGenFramebuffers(1,&copyFramebuffer);
 const char* vertex="#version 300 es\nprecision highp float;out vec2 uv;uniform int eye;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=vec2((p.x+float(eye))*0.5,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
 const char* fragment="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D frame;uniform int format;uniform bool preserveAlpha;out vec4 color;void main(){color=texture(frame,uv);if(format==44||format==50)color.rgb=color.bgr;if(format==43||format==50){vec3 low=color.rgb/12.92;vec3 high=pow((color.rgb+0.055)/1.055,vec3(2.4));color.rgb=mix(low,high,step(vec3(0.04045),color.rgb));}if(!preserveAlpha)color.a=1.0;}";
 GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)throw std::runtime_error("GPU bridge program");eyeUniform=glGetUniformLocation(program,"eye");formatUniform=glGetUniformLocation(program,"format");alphaUniform=glGetUniformLocation(program,"preserveAlpha");glGenVertexArrays(1,&vao);
 server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);sockaddr_un address{};address.sun_family=AF_UNIX;if(path.size()>=sizeof(address.sun_path))throw std::runtime_error("GPU socket path");strcpy(address.sun_path,path.c_str());unlink(path.c_str());if(server<0||bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(server,1))throw std::runtime_error("GPU bridge socket");LOG("Android GPU bridge ready");
}
void GpuBridge::poll(){
 if(client<0){client=accept4(server,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(client<0)return;char control[CMSG_SPACE(sizeof(dmaFds))]{};iovec io{&hello,sizeof(hello)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);cmsghdr* header=CMSG_FIRSTHDR(&message);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(dmaFds));memcpy(CMSG_DATA(header),dmaFds,sizeof(dmaFds));if(sendmsg(client,&message,MSG_NOSIGNAL)!=sizeof(hello)){close(client);client=-1;return;}hasFrame=false;LOG("Linux renderer connected to DMA-BUF bridge");}
 QuestGpuPresent present{};ssize_t n=recv(client,&present,sizeof(present),MSG_DONTWAIT);if(n==0){close(client);client=-1;current=-1;}else if(n==sizeof(present)&&present.magic==QUEST_GPU_MAGIC&&present.slot<QUEST_GPU_BUFFERS){if(current>=0){LOG("Renderer submitted an unreleased frame");close(client);client=-1;current=-1;return;}current=present.slot;frameId=present.frame_id;frameFormat=present.vk_format;framePose=present;if(frameId<=2)LOG("Linux frame %llu format=%u received",(unsigned long long)frameId,frameFormat);glBindFramebuffer(GL_READ_FRAMEBUFFER,copyFramebuffer);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[current],0);capture();glBindTexture(GL_TEXTURE_2D,history);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,hello.buffers[current].width,hello.buffers[current].height);glBindFramebuffer(GL_READ_FRAMEBUFFER,0);hasFrame=true;}
}
// Explicit, one-shot diagnostic of Linux's image before color conversion.
void GpuBridge::capture(){
 auto directory=path.substr(0,path.find_last_of('/'));auto request=directory+"/gpu-capture.request";
 if(access(request.c_str(),F_OK)!=0)return;unlink(request.c_str());
 uint32_t width=hello.buffers[current].width,height=hello.buffers[current].height;
 std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 auto output=fopen((directory+"/gpu-capture.rgba").c_str(),"wb");
 if(output){fwrite(&width,4,1,output);fwrite(&height,4,1,output);fwrite(&frameFormat,4,1,output);fwrite(pixels.data(),1,pixels.size(),output);fclose(output);LOG("Captured Linux GPU frame %llu format=%u",(unsigned long long)frameId,frameFormat);}
}
void GpuBridge::draw(int eye,bool preserveAlpha){if(!hasFrame)return;glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(program);glUniform1i(eyeUniform,eye);glUniform1i(formatUniform,frameFormat);glUniform1i(alphaUniform,preserveAlpha);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,history);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);}
void GpuBridge::finish(){if(current<0)return;glFinish();if(frameId<=2)LOG("OpenXR frame %llu submitted; releasing Linux buffer",(unsigned long long)frameId);QuestGpuRelease release{QUEST_GPU_MAGIC,static_cast<uint32_t>(current),frameId};if(client>=0&&send(client,&release,sizeof(release),MSG_NOSIGNAL)!=sizeof(release)){close(client);client=-1;}current=-1;}
void GpuBridge::shutdown(){if(client>=0)close(client);if(server>=0)close(server);if(!path.empty())unlink(path.c_str());auto destroyImage=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));for(int i=0;i<QUEST_GPU_BUFFERS;i++){if(images[i]&&destroyImage)destroyImage(display,images[i]);if(dmaFds[i]>=0)close(dmaFds[i]);if(buffers[i])AHardwareBuffer_release(buffers[i]);}glDeleteTextures(QUEST_GPU_BUFFERS,textures);if(history)glDeleteTextures(1,&history);if(copyFramebuffer)glDeleteFramebuffers(1,&copyFramebuffer);if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);}
