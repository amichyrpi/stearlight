#include "video_protocol.h"
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <deque>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <algorithm>
// Adapt only the private synthetic decoder. Real devices and all unrelated
// syscalls continue through libc; no kernel device permissions are changed.
namespace {
template<class T>T next(const char* name){return reinterpret_cast<T>(dlsym(RTLD_NEXT,name));}
int rawClose(int fd){static auto fn=next<int(*)(int)>("close");return fn(fd);}
constexpr unsigned Slots=16,InputSize=QUEST_VIDEO_MAX_PACKET;
struct Packet {unsigned index;timeval stamp;std::vector<unsigned char> data;};
struct Capture {bool queued=false,ready=false;unsigned fd=0,length=0,offset=0;timeval stamp{};};
struct Device {
 int fd,socket=-1;unsigned width=1280,height=720,stride=1280,codec=4,count=Slots;
 bool stateful=false,sourceKnown=false,sourceChange=false;int signalFd=-1;
 bool outputOn=false,captureOn=false,stopping=false,failed=false;unsigned sequence=0;
 std::mutex mutex;std::condition_variable wake;std::thread worker;
 std::deque<Packet> packets;std::deque<unsigned> completed;std::map<uint64_t,timeval> pending;
 Capture capture[Slots];
 explicit Device(int f):fd(f){stateful=getenv("QUEST_VIDEO_STATEFUL")!=nullptr;if(stateful){width=height=stride=128;signalFd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);}}
 ~Device(){stop();if(signalFd>=0)rawClose(signalFd);}
 unsigned ready(){unsigned flags=failed?EPOLLERR:0;if(sourceChange)flags|=EPOLLPRI;if(!completed.empty())flags|=EPOLLOUT;for(auto& c:capture)if(c.ready){flags|=EPOLLIN;break;}return flags;}
 void signal(){if(signalFd<0)return;uint64_t value=1;if(ready()){ssize_t result;do result=write(signalFd,&value,sizeof(value));while(result<0&&errno==EINTR);if(result<0&&errno!=EAGAIN)fprintf(stderr,"Quest video: event notification: %s\n",strerror(errno));}else while(read(signalFd,&value,sizeof(value))>0){}}
 void stop(){ {std::lock_guard<std::mutex> guard(mutex);stopping=true;wake.notify_all();if(socket>=0)::shutdown(socket,SHUT_RDWR);}
  if(worker.joinable())worker.join();if(socket>=0){rawClose(socket);socket=-1;}}
 void run();
 bool exchange(const QuestVideoRequest& r,const void* payload,QuestVideoResponse& response,std::vector<unsigned char>& data);
};
std::mutex devicesLock;std::map<int,std::shared_ptr<Device>> devices;
std::mutex discoveryLock;std::map<FILE*,std::unique_ptr<char[]>> discovery;
std::mutex epollsLock;std::map<int,std::map<int,epoll_event>> epolls;
std::shared_ptr<Device> device(int fd){std::lock_guard<std::mutex> guard(devicesLock);auto it=devices.find(fd);return it==devices.end()?nullptr:it->second;}
const char* socketPath(){const char* s=getenv("QUEST_VIDEO_SOCKET");return s?s:"/run/stearlight/quest/video.sock";}
bool transfer(int fd,void* data,size_t size,bool sendData){auto p=static_cast<unsigned char*>(data);while(size){ssize_t n=sendData?send(fd,p,size,MSG_NOSIGNAL):recv(fd,p,size,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;size-=n;}return true;}
bool Device::exchange(const QuestVideoRequest& r,const void* payload,QuestVideoResponse& response,std::vector<unsigned char>& data){
 if(!transfer(socket,const_cast<QuestVideoRequest*>(&r),sizeof(r),true)||(r.bytes&&!transfer(socket,const_cast<void*>(payload),r.bytes,true))||!transfer(socket,&response,sizeof(response),false))return false;
 if(response.magic!=QUEST_VIDEO_MAGIC||response.status<0||response.width>QUEST_VIDEO_MAX_DIMENSION||response.height>QUEST_VIDEO_MAX_DIMENSION||uint64_t(response.width)*response.height>QUEST_VIDEO_MAX_PIXELS||response.bytes>QUEST_VIDEO_MAX_FRAME)return false;
 data.resize(response.bytes);return !response.bytes||transfer(socket,data.data(),data.size(),false);
}
void Device::run(){
 {std::lock_guard<std::mutex> guard(mutex);socket=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);}
 sockaddr_un address{};address.sun_family=AF_UNIX;strncpy(address.sun_path,socketPath(),sizeof(address.sun_path)-1);
 QuestVideoResponse response{};std::vector<unsigned char> frame;
 QuestVideoRequest config{QUEST_VIDEO_MAGIC,QUEST_VIDEO_CONFIGURE,0,width,height,codec,0};
 bool ok=socket>=0&&!connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))&&exchange(config,nullptr,response,frame);
 fprintf(stderr,"Quest video: decoder %ux%u codec=%u connected=%d\n",width,height,codec,ok);
 while(ok){
  Packet packet{};bool submit=false;
  {std::unique_lock<std::mutex> guard(mutex);wake.wait_for(guard,std::chrono::milliseconds(5),[&]{return stopping||(!packets.empty()&&outputOn&&(captureOn||stateful));});
   if(stopping)break;if(!outputOn||(!stateful&&!captureOn))continue;
   if(!packets.empty()){packet=std::move(packets.front());packets.pop_front();submit=true;}else if(pending.empty())continue;}
  uint64_t stamp=uint64_t(packet.stamp.tv_sec)*1000000+packet.stamp.tv_usec;
  if(submit){std::lock_guard<std::mutex> guard(mutex);pending[stamp]=packet.stamp;}
  QuestVideoRequest r{QUEST_VIDEO_MAGIC,submit?QUEST_VIDEO_PACKET:QUEST_VIDEO_POLL,unsigned(packet.data.size()),0,0,0,stamp};
  ok=exchange(r,packet.data.data(),response,frame);
  {std::unique_lock<std::mutex> guard(mutex);if(submit){completed.push_back(packet.index);signal();wake.notify_all();}
   if(!ok)break;if(response.status!=0)continue;
   if(stateful&&(!sourceKnown||response.width!=width||response.height!=height)){
    width=response.width;height=response.height;stride=width;sourceKnown=true;sourceChange=true;captureOn=false;signal();wake.notify_all();
    wake.wait(guard,[&]{return stopping||captureOn;});if(stopping)break;
   }
   if(response.bytes!=size_t(response.width)*response.height*3/2||response.width>width||response.height>height){ok=false;break;}
   auto available=[&]{for(unsigned i=0;i<count;i++)if(capture[i].queued&&!capture[i].ready)return int(i);return -1;};
   wake.wait(guard,[&]{return stopping||available()>=0;});if(stopping)break;
   auto& c=capture[available()];size_t size=size_t(stride)*height*3/2;
   if(c.length<size+c.offset){fprintf(stderr,"Quest video: capture buffer too small: %u need=%zu\n",c.length,size+c.offset);ok=false;break;}
   void* mapped=mmap(nullptr,c.length,PROT_READ|PROT_WRITE,MAP_SHARED,c.fd,0);
   if(mapped==MAP_FAILED){fprintf(stderr,"Quest video: capture map fd=%u size=%u: %s\n",c.fd,c.length,strerror(errno));ok=false;break;}
   dma_buf_sync sync{DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE};static auto ioctlReal=next<int(*)(int,unsigned long,...)>("ioctl");ioctlReal(c.fd,DMA_BUF_IOCTL_SYNC,&sync);
   auto dst=static_cast<unsigned char*>(mapped)+c.offset;memset(dst,16,size_t(stride)*height);memset(dst+size_t(stride)*height,128,size_t(stride)*height/2);
   for(unsigned y=0;y<response.height;y++)memcpy(dst+size_t(y)*stride,frame.data()+size_t(y)*response.width,response.width);
   for(unsigned y=0;y<response.height/2;y++)memcpy(dst+size_t(stride)*height+size_t(y)*stride,frame.data()+size_t(response.width)*response.height+size_t(y)*response.width,response.width);
   sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;ioctlReal(c.fd,DMA_BUF_IOCTL_SYNC,&sync);munmap(mapped,c.length);
   auto it=pending.find(response.timestamp);c.stamp=it==pending.end()?timeval{long(response.timestamp/1000000),long(response.timestamp%1000000)}:it->second;
   if(it!=pending.end())pending.erase(it);c.ready=true;signal();wake.notify_all();
   if(++sequence%300==1)fprintf(stderr,"Quest video: frames=%u %ux%u capture stride=%u\n",sequence,response.width,response.height,stride);
  }
 }
 {std::lock_guard<std::mutex> guard(mutex);failed=!stopping;signal();wake.notify_all();if(!ok&&!stopping)fprintf(stderr,"Quest video: decoder transport/output failed\n");}
}
int fakeOpen(const char* path){
 bool allocator=!strcmp(path,"/dev/quest-video-memory");
 if(!allocator&&strcmp(path,"/dev/video0"))return -2;
 struct stat s{};if(stat(socketPath(),&s)||!S_ISSOCK(s.st_mode))return -2;
 if(allocator)return memfd_create("quest-video-memory",MFD_CLOEXEC);
 int fd=memfd_create("quest-v4l2",MFD_CLOEXEC);if(fd<0)return -1;
 if(ftruncate(fd,off_t(Slots)*InputSize)){rawClose(fd);return -1;}
 {std::lock_guard<std::mutex> guard(devicesLock);devices.emplace(fd,std::make_shared<Device>(fd));}return fd;
}
bool sharedVideoBuffer(int fd){
 char path[64],target[128];snprintf(path,sizeof(path),"/proc/self/fd/%d",fd);
 ssize_t n=readlink(path,target,sizeof(target)-1);if(n<0)return false;target[n]=0;
 return strstr(target,"memfd:quest-video-buffer")!=nullptr;
}
int invalid(int error=EINVAL){errno=error;return -1;}
}
// Frame's VR player asks the platform command for its decode device. Answer
// only this query inside the private player adapter, without spawning a
// platform utility that is absent on Android.
extern "C" FILE* popen(const char* command,const char* mode){
 if(!strcmp(command,"vrdevice_path video_decode")&&!strcmp(mode,"r")){
  struct stat s{};if(stat(socketPath(),&s)||!S_ISSOCK(s.st_mode)){errno=ENODEV;return nullptr;}
  auto data=std::make_unique<char[]>(13);strcpy(data.get(),"/dev/video0\n");
  FILE* result=fmemopen(data.get(),strlen(data.get()),"r");
  if(result){std::lock_guard<std::mutex> guard(discoveryLock);discovery.emplace(result,std::move(data));}return result;
 }
 static auto fn=next<FILE*(*)(const char*,const char*)>("popen");return fn(command,mode);
}
extern "C" int pclose(FILE* file){
 {std::lock_guard<std::mutex> guard(discoveryLock);auto it=discovery.find(file);if(it!=discovery.end()){int result=fclose(file);discovery.erase(it);return result;}}
 static auto fn=next<int(*)(FILE*)>("pclose");return fn(file);
}
extern "C" void* SDL_LoadObject(const char* path){
 static auto fn=next<void*(*)(const char*)>("SDL_LoadObject");
 if(!strcmp(path,"libdrm.so.2")){
  fprintf(stderr,"Quest video: loading headless streaming buffer allocator\n");
  return fn("/opt/quest/libquest_streaming_drm.so");
 }
 return fn(path);
}
extern "C" int open(const char* path,int flags,...){int fd=fakeOpen(path);if(fd!=-2)return fd;mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}static auto fn=next<int(*)(const char*,int,...)>("open");return fn(path,flags,mode);}
extern "C" int open64(const char* path,int flags,...){int fd=fakeOpen(path);if(fd!=-2)return fd;mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=va_arg(a,int);va_end(a);}static auto fn=next<int(*)(const char*,int,...)>("open64");return fn(path,flags,mode);}
extern "C" int close(int fd){std::shared_ptr<Device> d;{std::lock_guard<std::mutex> guard(devicesLock);auto it=devices.find(fd);if(it!=devices.end()){d=it->second;devices.erase(it);}}{std::lock_guard<std::mutex> guard(epollsLock);epolls.erase(fd);for(auto& ep:epolls)ep.second.erase(fd);}if(d)d->stop();return rawClose(fd);}
extern "C" int ioctl(int fd,unsigned long request,...){
 va_list args;va_start(args,request);void* arg=va_arg(args,void*);va_end(args);auto d=device(fd);
 // These are coherent shared-memory buffers used by Steam's CPU upload
 // fallback. They are not DMA-BUFs and need no device cache synchronization.
 if(request==DMA_BUF_IOCTL_SYNC&&sharedVideoBuffer(fd))return 0;
 if(!d){static auto fn=next<int(*)(int,unsigned long,...)>("ioctl");return fn(fd,request,arg);}
 if(!arg)return invalid(EFAULT);std::unique_lock<std::mutex> guard(d->mutex);
 if(getenv("QUEST_VIDEO_TRACE"))fprintf(stderr,"Quest video: ioctl 0x%lx first=%u\n",request,*static_cast<unsigned*>(arg));
 switch(request){
 case VIDIOC_QUERYCAP:{auto c=static_cast<v4l2_capability*>(arg);memset(c,0,sizeof(*c));strcpy(reinterpret_cast<char*>(c->driver),"quest-mediacodec");strcpy(reinterpret_cast<char*>(c->card),"Quest Android video decoder");c->version=KERNEL_VERSION(6,1,0);c->device_caps=V4L2_CAP_VIDEO_M2M_MPLANE|V4L2_CAP_STREAMING;c->capabilities=c->device_caps|V4L2_CAP_DEVICE_CAPS;return 0;}
 case VIDIOC_ENUM_FMT:{auto f=static_cast<v4l2_fmtdesc*>(arg);if(f->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){if(f->index>1)return invalid();f->pixelformat=f->index?V4L2_PIX_FMT_HEVC:V4L2_PIX_FMT_H264;f->flags=V4L2_FMT_FLAG_COMPRESSED;}else{if(f->index)return invalid();f->pixelformat=V4L2_PIX_FMT_NV12;}return 0;}
 case VIDIOC_S_FMT:case VIDIOC_G_FMT:case VIDIOC_TRY_FMT:{auto f=static_cast<v4l2_format*>(arg);auto& p=f->fmt.pix_mp;
  if(getenv("QUEST_VIDEO_TRACE"))fprintf(stderr,"Quest video: format %ux%u fourcc=0x%x\n",p.width,p.height,p.pixelformat);
  if(f->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){if(request==VIDIOC_S_FMT){if(p.pixelformat!=V4L2_PIX_FMT_H264&&p.pixelformat!=V4L2_PIX_FMT_HEVC)return invalid();d->codec=p.pixelformat==V4L2_PIX_FMT_H264?4:5;if(!d->stateful&&p.width&&p.height){d->width=p.width;d->height=p.height;d->stride=p.width;}}p.num_planes=1;p.plane_fmt[0].sizeimage=InputSize;}
  else if(f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){if(request==VIDIOC_S_FMT&&p.width&&p.height){d->width=p.width;d->height=p.height;d->stride=p.width;}p.width=d->width;p.height=d->height;p.pixelformat=V4L2_PIX_FMT_NV12;p.num_planes=1;p.plane_fmt[0].bytesperline=d->stride;p.plane_fmt[0].sizeimage=d->stride*d->height*3/2;p.colorspace=V4L2_COLORSPACE_REC709;p.ycbcr_enc=V4L2_YCBCR_ENC_709;p.quantization=V4L2_QUANTIZATION_LIM_RANGE;}
  else return invalid();p.field=V4L2_FIELD_NONE;return 0;}
 case VIDIOC_REQBUFS:{auto r=static_cast<v4l2_requestbuffers*>(arg);r->count=std::min(r->count,Slots);if(r->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)d->count=r->count;return 0;}
 case VIDIOC_QUERYBUF:{auto b=static_cast<v4l2_buffer*>(arg);if(b->index>=Slots||b->length<1||!b->m.planes)return invalid();b->length=1;b->m.planes[0].length=InputSize;b->m.planes[0].m.mem_offset=b->index*InputSize;return 0;}
 case VIDIOC_QBUF:{auto b=static_cast<v4l2_buffer*>(arg);if(b->index>=Slots||b->length<1||!b->m.planes)return invalid();auto& p=b->m.planes[0];
  if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){unsigned size=p.bytesused?p.bytesused:b->bytesused;if(!size||size>InputSize||p.data_offset>size)return invalid();Packet packet{b->index,b->timestamp,{}};packet.data.resize(size-p.data_offset);ssize_t n=pread(fd,packet.data.data(),packet.data.size(),off_t(b->index)*InputSize+p.data_offset);if(n!=ssize_t(packet.data.size()))return invalid(EIO);d->packets.push_back(std::move(packet));}
  else if(b->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){
   if(b->memory!=V4L2_MEMORY_DMABUF||p.m.fd<0)return invalid();
   struct stat allocation{};if(fstat(p.m.fd,&allocation))return -1;
   size_t required=size_t(d->stride)*d->height*3/2+p.data_offset;
   if(allocation.st_size<0||uint64_t(allocation.st_size)<required){fprintf(stderr,"Quest video: capture allocation fd=%d bytes=%lld length=%u offset=%u need=%zu\n",p.m.fd,(long long)allocation.st_size,p.length,p.data_offset,required);return invalid();}
   // Steam queues an exported allocation with length=0. Resolve the real
   // allocation size and return it as the driver would, retaining the FD.
   if(!p.length)p.length=std::min<uint64_t>(allocation.st_size,UINT32_MAX);
   if(p.length<required||p.length>uint64_t(allocation.st_size))return invalid();
   auto& c=d->capture[b->index];c.fd=p.m.fd;c.length=p.length;c.offset=p.data_offset;c.queued=true;c.ready=false;
  }
  else return invalid();d->wake.notify_all();return 0;}
 case VIDIOC_DQBUF:{auto b=static_cast<v4l2_buffer*>(arg);if(!b->m.planes||b->length<1)return invalid();b->flags=V4L2_BUF_FLAG_TIMESTAMP_COPY;b->field=V4L2_FIELD_NONE;b->length=1;
  if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){if(d->completed.empty())return invalid(d->failed?EIO:EAGAIN);b->index=d->completed.front();d->completed.pop_front();b->m.planes[0].length=InputSize;d->signal();return 0;}
  if(b->type!=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)return invalid();for(unsigned i=0;i<d->count;i++){auto& c=d->capture[i];if(!c.ready)continue;b->index=i;b->timestamp=c.stamp;b->sequence=d->sequence;b->m.planes[0].bytesused=d->stride*d->height*3/2;b->m.planes[0].length=c.length;b->m.planes[0].data_offset=c.offset;b->m.planes[0].m.fd=c.fd;c.ready=false;c.queued=false;d->signal();return 0;}return invalid(d->failed?EIO:EAGAIN);}
 case VIDIOC_STREAMON:{auto type=*static_cast<int*>(arg);if(type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)d->outputOn=true;else if(type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)d->captureOn=true;else return invalid();if(d->outputOn&&(d->captureOn||d->stateful)&&!d->worker.joinable())d->worker=std::thread(&Device::run,d.get());d->wake.notify_all();return 0;}
 case VIDIOC_STREAMOFF:{auto type=*static_cast<int*>(arg);if(type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){d->outputOn=false;d->packets.clear();d->completed.clear();}else {d->captureOn=false;for(auto& c:d->capture)c.queued=c.ready=false;}return 0;}
 case VIDIOC_SUBSCRIBE_EVENT:case VIDIOC_UNSUBSCRIBE_EVENT:return 0;
 case VIDIOC_DQEVENT:{if(!d->sourceChange)return invalid(EAGAIN);auto e=static_cast<v4l2_event*>(arg);memset(e,0,sizeof(*e));e->type=V4L2_EVENT_SOURCE_CHANGE;e->u.src_change.changes=V4L2_EVENT_SRC_CH_RESOLUTION;d->sourceChange=false;d->signal();return 0;}
 case VIDIOC_S_CTRL:{auto c=static_cast<v4l2_control*>(arg);if(c->id==V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY||c->id==V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE)return 0;return invalid();}
 default:fprintf(stderr,"Quest video: unsupported ioctl 0x%lx\n",request);return invalid(ENOTTY);
 }
}
extern "C" int epoll_ctl(int epoll,int op,int fd,epoll_event* event){
 static auto fn=next<int(*)(int,int,int,epoll_event*)>("epoll_ctl");auto d=device(fd);if(!d||d->signalFd<0)return fn(epoll,op,fd,event);
 epoll_event signal{};if(event){signal=*event;signal.events=EPOLLIN;}
 int result=fn(epoll,op,d->signalFd,event?&signal:nullptr);
 if(!result){std::lock_guard<std::mutex> guard(epollsLock);if(op==EPOLL_CTL_DEL)epolls[epoll].erase(fd);else epolls[epoll][fd]=*event;}return result;
}
extern "C" int epoll_wait(int epoll,epoll_event* events,int maxEvents,int timeout){
 static auto fn=next<int(*)(int,epoll_event*,int,int)>("epoll_wait");int count=fn(epoll,events,maxEvents,timeout);if(count<=0)return count;
 std::map<int,epoll_event> registered;{std::lock_guard<std::mutex> guard(epollsLock);auto it=epolls.find(epoll);if(it==epolls.end())return count;registered=it->second;}
 for(int i=0;i<count;i++)for(auto& entry:registered)if(events[i].data.u64==entry.second.data.u64){auto d=device(entry.first);if(d){std::lock_guard<std::mutex> guard(d->mutex);events[i].events=d->ready()&(entry.second.events|EPOLLERR|EPOLLHUP);}break;}
 return count;
}
extern "C" int poll(pollfd* fds,nfds_t count,int timeout){
 static auto fn=next<int(*)(pollfd*,nfds_t,int)>("poll");bool ours=false;for(nfds_t i=0;i<count;i++)if(device(fds[i].fd)){ours=true;break;}if(!ours)return fn(fds,count,timeout);
 auto start=std::chrono::steady_clock::now();for(;;){int ready=0;std::vector<pollfd> real(fds,fds+count);
  for(nfds_t i=0;i<count;i++){fds[i].revents=0;auto d=device(fds[i].fd);if(!d)continue;real[i].fd=-1;std::lock_guard<std::mutex> guard(d->mutex);fds[i].revents=d->ready()&(fds[i].events|POLLERR|POLLHUP);if(fds[i].revents)ready++;}
  fn(real.data(),count,0);for(nfds_t i=0;i<count;i++)if(real[i].revents){fds[i].revents=real[i].revents;ready++;}
  if(ready||timeout==0)return ready;if(timeout>0&&std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()>=timeout)return 0;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
