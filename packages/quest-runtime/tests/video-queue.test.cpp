#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <cstdio>

int main(){
 constexpr unsigned width=128,height=128,bytes=width*height*3/2,offset=64;
 int video=open("/dev/video0",O_RDWR);assert(video>=0);
 v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
 format.fmt.pix_mp.width=width;format.fmt.pix_mp.height=height;format.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_H264;
 assert(!ioctl(video,VIDIOC_S_FMT,&format));format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 format.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;assert(!ioctl(video,VIDIOC_S_FMT,&format));
 int fd[3];unsigned char* mapped[3];
 auto queueCapture=[&](int i){v4l2_plane plane{};plane.m.fd=fd[i];plane.data_offset=offset;
  v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=V4L2_MEMORY_DMABUF;b.index=i;b.length=1;b.m.planes=&plane;
  assert(!ioctl(video,VIDIOC_QBUF,&b));};
 for(int i=0;i<3;i++){fd[i]=memfd_create("quest-video-buffer",MFD_CLOEXEC);assert(fd[i]>=0);
  assert(!ftruncate(fd[i],bytes+offset*2));mapped[i]=static_cast<unsigned char*>(mmap(nullptr,bytes+offset*2,PROT_READ|PROT_WRITE,MAP_SHARED,fd[i],0));
  assert(mapped[i]!=MAP_FAILED);memset(mapped[i],0xa5,bytes+offset*2);queueCapture(i);}
 v4l2_requestbuffers request{};request.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;request.memory=V4L2_MEMORY_DMABUF;request.count=3;
 assert(!ioctl(video,VIDIOC_REQBUFS,&request));
 int type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;assert(!ioctl(video,VIDIOC_STREAMON,&type));
 type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;assert(!ioctl(video,VIDIOC_STREAMON,&type));
 auto packet=[&](int index,int timestamp){char data=1;assert(pwrite(video,&data,1,off_t(index)*8*1024*1024)==1);
  v4l2_plane plane{};plane.bytesused=1;v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;b.memory=V4L2_MEMORY_MMAP;
  b.index=index;b.timestamp.tv_usec=timestamp;b.m.planes=&plane;b.length=1;assert(!ioctl(video,VIDIOC_QBUF,&b));};
 auto complete=[&](){v4l2_plane plane{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;b.m.planes=&plane;b.length=1;
  for(int retry=0;retry<1000;retry++){if(!ioctl(video,VIDIOC_DQBUF,&b))return;assert(errno==EAGAIN);usleep(1000);}assert(false);};
 auto take=[&](int index,int timestamp,int sequence){v4l2_plane plane{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.m.planes=&plane;b.length=1;
  assert(!ioctl(video,VIDIOC_DQBUF,&b));assert(b.index==unsigned(index)&&b.timestamp.tv_usec==timestamp&&b.sequence==unsigned(sequence));
  assert(plane.data_offset==offset&&plane.bytesused==bytes);
  assert(mapped[index][offset]==40+timestamp&&mapped[index][offset+width*height]==128);
  for(unsigned i=0;i<offset;i++)assert(mapped[index][i]==0xa5&&mapped[index][offset+bytes+i]==0xa5);};
 packet(0,1);packet(1,2);packet(2,3);complete();complete();complete();
 take(0,1,1);queueCapture(0);packet(0,4);complete();
 // Buffer 0 now holds frame 4. It must not jump ahead of frames 2 and 3.
 take(1,2,2);take(2,3,3);take(0,4,4);
 // A stalled consumer must not stop reference-frame decoding. Keep only
 // recent completed images, preserving ownership of dequeued buffers.
 for(int i=0;i<3;i++)queueCapture(i);
 packet(0,5);packet(1,6);packet(2,7);complete();complete();complete();
 packet(0,8);complete();take(1,6,6);take(2,7,7);take(0,8,8);
 packet(0,9);complete(); // All captures belong to the consumer: discard 9.
 queueCapture(0);packet(0,10);complete();take(0,10,10);
 close(video);for(int i=0;i<3;i++){munmap(mapped[i],bytes+offset*2);close(fd[i]);}
 puts("Video queue: presentation order, bounded backlog, buffer ownership and pixel bounds passed");
}
