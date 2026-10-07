#pragma once
#include <stdint.h>
enum { QUEST_GPU_MAGIC=0x53475131,QUEST_GPU_VERSION=3,QUEST_GPU_BUFFERS=3 };
struct QuestGpuBuffer { uint32_t width,height,stride,format;uint64_t allocation_size; };
struct QuestGpuHello { uint32_t magic,version,count,reserved;struct QuestGpuBuffer buffers[QUEST_GPU_BUFFERS]; };
// Input views sampled when Linux acquires this image, before rendering starts.
struct QuestGpuView { float position[3],orientation[4],fov[4]; };
struct QuestGpuPresent {
 uint32_t magic,slot;uint64_t frame_id;uint32_t vk_format,pose_sequence;
 uint64_t predicted_monotonic_ns;struct QuestGpuView views[2];
};
struct QuestGpuRelease { uint32_t magic,slot;uint64_t frame_id; };
#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <string>
class GpuBridge {
 int server=-1,client=-1,current=-1;uint64_t frameId=0;std::string path;
 AHardwareBuffer* buffers[QUEST_GPU_BUFFERS]{};EGLImageKHR images[QUEST_GPU_BUFFERS]{};GLuint textures[QUEST_GPU_BUFFERS]{};
 int dmaFds[QUEST_GPU_BUFFERS]={-1,-1,-1};
 GLuint program=0,vao=0,history=0,copyFramebuffer=0;bool hasFrame=false;GLint eyeUniform=-1,formatUniform=-1,alphaUniform=-1;uint32_t frameFormat=37;QuestGpuHello hello{};QuestGpuPresent framePose{};EGLDisplay display=EGL_NO_DISPLAY;
public:
 void initialize(EGLDisplay,const std::string&,uint32_t,uint32_t);
 void poll();void draw(int,bool preserveAlpha=false);void finish();void shutdown();
 void capture();
 bool ready()const{return current>=0;}
 const QuestGpuPresent& inputPose()const{return framePose;}
};
#endif
