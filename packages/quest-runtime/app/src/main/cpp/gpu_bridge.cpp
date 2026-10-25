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
#include <poll.h>
#include <pthread.h>
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
 const char* vertex="#version 300 es\nprecision highp float;out vec2 uv;uniform int eye;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=vec2((p.x+float(eye))*0.5,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
 const char* fragment="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D frame;uniform int format;uniform bool preserveAlpha;out vec4 color;void main(){color=texture(frame,uv);if(format==44||format==50)color.rgb=color.bgr;if(format==43||format==50){vec3 low=color.rgb/12.92;vec3 high=pow((color.rgb+0.055)/1.055,vec3(2.4));color.rgb=mix(low,high,step(vec3(0.04045),color.rgb));}if(!preserveAlpha)color.a=1.0;}";
 GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)throw std::runtime_error("GPU bridge program");eyeUniform=glGetUniformLocation(program,"eye");formatUniform=glGetUniformLocation(program,"format");alphaUniform=glGetUniformLocation(program,"preserveAlpha");glGenVertexArrays(1,&vao);
 server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);sockaddr_un address{};address.sun_family=AF_UNIX;if(path.size()>=sizeof(address.sun_path))throw std::runtime_error("GPU socket path");strcpy(address.sun_path,path.c_str());unlink(path.c_str());if(server<0||bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(server,1))throw std::runtime_error("GPU bridge socket");LOG("Android GPU bridge ready");
 EGLint configId=0,configCount=0;EGLConfig config{};
 if(!eglQueryContext(display,eglGetCurrentContext(),EGL_CONFIG_ID,&configId))throw std::runtime_error("Copy context config");
 EGLint configAttributes[]={EGL_CONFIG_ID,configId,EGL_NONE};
 if(!eglChooseConfig(display,configAttributes,&config,1,&configCount)||configCount!=1)throw std::runtime_error("Copy context config lookup");
 EGLint contextAttributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surfaceAttributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
 copyContext=eglCreateContext(display,config,eglGetCurrentContext(),contextAttributes);
 copySurface=eglCreatePbufferSurface(display,config,surfaceAttributes);
 if(copyContext==EGL_NO_CONTEXT||copySurface==EGL_NO_SURFACE)throw std::runtime_error("Shared copy context");
 // The main context has finished allocating the shared imports/program.
 glFinish();copyThread=std::thread([this]{copyFrames();});
}
void GpuBridge::copyFrames(){
 pthread_setname_np(pthread_self(),"QuestGpuCopy");
 try{
  if(!eglMakeCurrent(display,copySurface,copySurface,copyContext))throw std::runtime_error("Copy context activation");
  glGenFramebuffers(1,&copyFramebuffer);
  LOG("Dedicated GPU copy context ready");
  while(!stopping.load()){
   pollfd event{client>=0?client:server,POLLIN,0};
   int result=::poll(&event,1,20);if(result<0&&errno!=EINTR)throw std::runtime_error("GPU copy socket poll");
   if(result<=0)continue;
   if(client<0){
    client=accept4(server,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(client<0)continue;
    char control[CMSG_SPACE(sizeof(dmaFds))]{};iovec io{&hello,sizeof(hello)};msghdr message{};
    message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=control;message.msg_controllen=sizeof(control);
    cmsghdr* header=CMSG_FIRSTHDR(&message);header->cmsg_level=SOL_SOCKET;header->cmsg_type=SCM_RIGHTS;header->cmsg_len=CMSG_LEN(sizeof(dmaFds));memcpy(CMSG_DATA(header),dmaFds,sizeof(dmaFds));
    if(sendmsg(client,&message,MSG_NOSIGNAL)!=sizeof(hello)){close(client);client=-1;continue;}
    LOG("Linux renderer connected to DMA-BUF bridge");continue;
   }
   QuestGpuPresent present{};ssize_t size=recv(client,&present,sizeof(present),MSG_DONTWAIT|MSG_TRUNC);
   if(size<0&&(errno==EAGAIN||errno==EINTR))continue;
   if(size!=sizeof(present)||present.magic!=QUEST_GPU_MAGIC||present.slot>=QUEST_GPU_BUFFERS){close(client);client=-1;continue;}
   copy(present);
   QuestGpuRelease release{QUEST_GPU_MAGIC,present.slot,present.frame_id};
   if(send(client,&release,sizeof(release),MSG_NOSIGNAL)!=sizeof(release)){close(client);client=-1;}
   // Keep explicit paired readbacks on the same image. This diagnostic wait
   // runs only for a requested capture, never during normal rendering.
   auto request=path.substr(0,path.find_last_of('/'))+"/xr-eye-capture.request";
   auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
   while(!stopping.load()&&access(request.c_str(),F_OK)==0&&std::chrono::steady_clock::now()<deadline)::poll(nullptr,0,10);
  }
 }catch(const std::exception& error){LOG("GPU copy worker stopped: %s",error.what());}
 if(client>=0){close(client);client=-1;}
 if(copyFramebuffer){glDeleteFramebuffers(1,&copyFramebuffer);copyFramebuffer=0;}
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
}
void GpuBridge::copy(const QuestGpuPresent& present){
 int slot;
 {std::lock_guard<std::mutex> guard(historyLock);slot=historySlots.beginWrite();}
 if(slot<0)throw std::runtime_error("GPU history ownership");
 auto& image=histories[slot];const auto& buffer=hello.buffers[present.slot];
 auto storage=GL_RGBA8;
 auto allocate=[&](GLenum format){
  if(image.texture)glDeleteTextures(1,&image.texture);
  glGenTextures(1,&image.texture);glBindTexture(GL_TEXTURE_2D,image.texture);glTexStorage2D(GL_TEXTURE_2D,1,format,buffer.width,buffer.height);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);image.storage=format;
 };
 if(image.storage!=static_cast<GLenum>(storage))allocate(storage);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,copyFramebuffer);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[present.slot],0);
 capture(present);
 glBindTexture(GL_TEXTURE_2D,image.texture);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,buffer.width,buffer.height);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
 auto start=std::chrono::steady_clock::now();glFinish();
 auto done=std::chrono::steady_clock::now();completionMs+=std::chrono::duration<double,std::milli>(done-start).count();
 image.pose=present;
 {std::lock_guard<std::mutex> guard(historyLock);if(!historySlots.publish(slot))throw std::runtime_error("GPU history publication");}
 if(present.frame_id<=2)LOG("Linux frame %llu copied; releasing shared buffer",(unsigned long long)present.frame_id);
 if(reportTime.time_since_epoch().count()==0)reportTime=done;
 ++reportFrames;double seconds=std::chrono::duration<double>(done-reportTime).count();
 if(seconds>=5){LOG("Linux handoffs %.1f fps; GPU copy wait %.2f ms/frame",reportFrames/seconds,completionMs/reportFrames);reportTime=done;reportFrames=0;completionMs=0;}
}
void GpuBridge::poll(){
 // The copy thread may recycle the previously held texture after adoption.
 // Test only our eye draws, without waiting for xrEndFrame's compositor work.
 bool readsComplete=true;
 if(readFence){
  GLenum result=glClientWaitSync(readFence,0,0);
  readsComplete=result!=GL_TIMEOUT_EXPIRED;
  if(result==GL_WAIT_FAILED){LOG("Eye-read fence failed; completing reads synchronously");glFinish();}
  if(readsComplete){glDeleteSync(readFence);readFence=nullptr;}
 }
 std::lock_guard<std::mutex> guard(historyLock);int slot=historySlots.adopt(readsComplete);if(slot<0)return;
 const auto& image=histories[slot];history=image.texture;
 framePose=image.pose;frameId=framePose.frame_id;frameFormat=framePose.vk_format;current=slot;hasFrame=true;
 auto now=std::chrono::steady_clock::now();if(sceneReport.time_since_epoch().count()==0)sceneReport=now;
 ++sceneFrames;double seconds=std::chrono::duration<double>(now-sceneReport).count();
 if(seconds>=5){LOG("Scene updates %.1f fps",sceneFrames/seconds);sceneReport=now;sceneFrames=0;}
}
void GpuBridge::endRead(){
 if(!hasFrame)return;
 // A replacement fence covers earlier reads on this same GL command stream.
 if(readFence)glDeleteSync(readFence);
 readFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
 if(!readFence){LOG("Eye-read fence unavailable; completing reads synchronously");glFinish();}
 else glFlush();
}
// Explicit, one-shot diagnostic of Linux's image before color conversion.
void GpuBridge::capture(const QuestGpuPresent& present){
 auto directory=path.substr(0,path.find_last_of('/'));auto request=directory+"/gpu-capture.request";
 if(access(request.c_str(),F_OK)!=0&&access((directory+"/xr-eye-capture.request").c_str(),F_OK)!=0)return;unlink(request.c_str());
 uint32_t width=hello.buffers[present.slot].width,height=hello.buffers[present.slot].height;
 std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 auto output=fopen((directory+"/gpu-capture.rgba").c_str(),"wb");
 if(output){fwrite(&width,4,1,output);fwrite(&height,4,1,output);fwrite(&present.vk_format,4,1,output);fwrite(pixels.data(),1,pixels.size(),output);fclose(output);captureFrame.store(present.frame_id);LOG("Captured Linux GPU frame %llu format=%u",(unsigned long long)present.frame_id,present.vk_format);}
}
void GpuBridge::draw(int eye,bool preserveAlpha){if(!hasFrame)return;
 glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(program);glUniform1i(eyeUniform,eye);glUniform1i(formatUniform,frameFormat);glUniform1i(alphaUniform,preserveAlpha);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,history);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
 // Pair the Linux capture with the actual right-eye image after conversion,
 // before OpenXR accepts it. Only an explicit one-shot request reads pixels.
 if(eye!=1)return;
 auto directory=path.substr(0,path.find_last_of('/'));auto request=directory+"/xr-eye-capture.request";
 if(access(request.c_str(),F_OK)!=0||captureFrame.load()!=frameId)return;unlink(request.c_str());
 GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);uint32_t width=viewport[2],height=viewport[3],format=43;
 std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 if(FILE* output=fopen((directory+"/xr-eye-capture.rgba").c_str(),"wb")){
  fwrite(&width,4,1,output);fwrite(&height,4,1,output);fwrite(&format,4,1,output);
  fwrite(pixels.data(),1,pixels.size(),output);fclose(output);LOG("Captured OpenXR right eye for Linux frame %llu",(unsigned long long)frameId);
 }
}
// Publication already follows the copy context's glFinish. Clearing freshness
// keeps the same held texture and pose for repeated OpenXR submissions.
void GpuBridge::finish(){current=-1;}
void GpuBridge::shutdown(){
 stopping.store(true);if(copyThread.joinable())copyThread.join();
 if(readFence){glFinish();glDeleteSync(readFence);readFence=nullptr;}
 if(server>=0){close(server);server=-1;}if(!path.empty())unlink(path.c_str());
 if(copySurface!=EGL_NO_SURFACE)eglDestroySurface(display,copySurface);
 if(copyContext!=EGL_NO_CONTEXT)eglDestroyContext(display,copyContext);
 auto destroyImage=reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
 for(int i=0;i<QUEST_GPU_BUFFERS;i++){if(images[i]&&destroyImage)destroyImage(display,images[i]);if(dmaFds[i]>=0)close(dmaFds[i]);if(buffers[i])AHardwareBuffer_release(buffers[i]);}
 glDeleteTextures(QUEST_GPU_BUFFERS,textures);
 for(auto& image:histories)if(image.texture)glDeleteTextures(1,&image.texture);
 if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);
}
