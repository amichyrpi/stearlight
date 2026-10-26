#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstdio>
#include "../linux/video_sequence.h"
#include <cstring>

int main(int argc,char** argv){
 // Sequence headers taken from real x265 Main10 streams, including escaped
 // RBSP bytes and the tall stereo layout used by the VR player.
 auto checkHeader=[](const char* hex,uint32_t expectedWidth,uint32_t expectedHeight){
  std::vector<uint8_t> packet{0,0,0,1,0x40,1,0x08,0,0,1};
  for(size_t i=0;i<std::strlen(hex);i+=2){unsigned byte=0;assert(sscanf(hex+i,"%2x",&byte)==1);packet.push_back(byte);}
  packet.insert(packet.end(),{0,0,0,1,0x44,1});uint32_t width=0,height=0;
  assert(quest::hevcDimensions(packet.data(),packet.size(),width,height)&&width==expectedWidth&&height==expectedHeight);
  for(size_t size=0;size<packet.size();size++){
   width=height=0;if(quest::hevcDimensions(packet.data(),size,width,height))assert(width&&height&&width<=8192&&height<=8192&&uint64_t(width)*height<=4096*4096);
  }
 };
 checkHeader("42010102200000030090000003000003003ca01020204d96e92930bc05a020000003002000000901",128,128);
 checkHeader("420101022000000300900000030000030099a002408004804d96e92930bc05a020000003002000000901",1152,4608);
 checkHeader("4201012220000003009000000300000300b7a002408004804d965d29084645d5",1152,4608); // Captured NVENC Main10 SPS.
 assert(argc==2);int video=open("/dev/video0",O_RDWR);assert(video>=0);
 v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;format.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_HEVC;
 assert(!ioctl(video,VIDIOC_S_FMT,&format));
 v4l2_requestbuffers request{};request.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;request.count=16;
 assert(!ioctl(video,VIDIOC_REQBUFS,&request)&&request.count==16);
 int type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;assert(!ioctl(video,VIDIOC_STREAMON,&type));
 char encoded=1;assert(pwrite(video,&encoded,1,0)==1);
 v4l2_plane plane{};plane.bytesused=1;v4l2_buffer buffer{};buffer.type=type;buffer.memory=V4L2_MEMORY_MMAP;
 buffer.length=1;buffer.m.planes=&plane;buffer.timestamp.tv_sec=713;assert(!ioctl(video,VIDIOC_QBUF,&buffer));
 // Stale reference pictures must complete without configuring Android.
 pollfd skipped{video,POLLOUT,0};assert(poll(&skipped,1,5000)==1&&(skipped.revents&POLLOUT));
 plane={};buffer={};buffer.type=type;buffer.length=1;buffer.m.planes=&plane;
 assert(!ioctl(video,VIDIOC_DQBUF,&buffer)&&buffer.index==0);
 const char* hex="00000001420101022000000300900000030000030099a002408004804d96e92930bc05a020000003002000000901";
 std::vector<uint8_t> header;
 for(size_t i=0;i<std::strlen(hex);i+=2){unsigned byte=0;assert(sscanf(hex+i,"%2x",&byte)==1);header.push_back(byte);}
 assert(pwrite(video,header.data(),header.size(),0)==ssize_t(header.size()));
 plane={};plane.bytesused=header.size();buffer={};buffer.type=type;buffer.length=1;buffer.m.planes=&plane;buffer.timestamp.tv_sec=713;
 assert(!ioctl(video,VIDIOC_QBUF,&buffer));
 pollfd event{video,POLLPRI|POLLIN,0};assert(poll(&event,1,5000)==1&&(event.revents&POLLPRI));
 v4l2_event source{};assert(!ioctl(video,VIDIOC_DQEVENT,&source)&&source.type==V4L2_EVENT_SOURCE_CHANGE);
 format={};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;assert(!ioctl(video,VIDIOC_G_FMT,&format));
 assert(format.fmt.pix_mp.width==1152&&format.fmt.pix_mp.height==4608);
 // The input completes before any picture or capture allocation.
 event={video,POLLOUT,0};assert(poll(&event,1,5000)==1&&(event.revents&POLLOUT));
 plane={};buffer={};buffer.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;buffer.length=1;buffer.m.planes=&plane;
 assert(!ioctl(video,VIDIOC_DQBUF,&buffer)&&buffer.index==0);
 assert(!ioctl(video,VIDIOC_S_FMT,&format));
 request={};request.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;request.count=16;
 assert(!ioctl(video,VIDIOC_REQBUFS,&request)&&request.count==3);
 constexpr unsigned bytes=1152*4608*3/2;int fd[3];unsigned char* pixels[3];
 for(unsigned i=0;i<3;i++){
  fd[i]=memfd_create("quest-video-buffer",MFD_CLOEXEC);assert(fd[i]>=0&&!ftruncate(fd[i],bytes));
  pixels[i]=static_cast<unsigned char*>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd[i],0));assert(pixels[i]!=MAP_FAILED);
  plane={};plane.m.fd=fd[i];buffer={};buffer.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;buffer.memory=V4L2_MEMORY_DMABUF;
  buffer.index=i;buffer.length=1;buffer.m.planes=&plane;assert(!ioctl(video,VIDIOC_QBUF,&buffer));
 }
 int marker=open(argv[1],O_CREAT|O_WRONLY,0600);assert(marker>=0);close(marker);
 type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;assert(!ioctl(video,VIDIOC_STREAMON,&type));
 event={video,POLLIN,0};assert(poll(&event,1,5000)==1&&(event.revents&POLLIN));
 plane={};buffer={};buffer.type=type;buffer.length=1;buffer.m.planes=&plane;
 assert(!ioctl(video,VIDIOC_DQBUF,&buffer)&&buffer.index<3&&buffer.timestamp.tv_sec==713&&buffer.timestamp.tv_usec==0);
 assert(plane.bytesused==bytes&&pixels[buffer.index][0]==81&&pixels[buffer.index][1152*4608]==90&&pixels[buffer.index][1152*4608+1]==240);
 close(video);for(int i=0;i<3;i++){munmap(pixels[i],bytes);close(fd[i]);}
 puts("Early format: source event, completed input, bounded captures and first picture passed");
}
