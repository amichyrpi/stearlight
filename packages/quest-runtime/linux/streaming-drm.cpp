#include <xf86drm.h>
#include <xf86drmMode.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <map>
#include <mutex>

// Private libdrm implementation loaded only by the OpenVR streaming player.
// There is no KMS display on Android. Expose a headless buffer allocator and
// one virtual connector; OpenVR still owns presentation. Exported memfds use
// Steam's existing CPU texture upload fallback, not EGL DMA-BUF import.
namespace {
std::mutex lock;
std::map<uint32_t,int> buffers;
uint32_t nextHandle=1;
uint32_t connectorId=1,encoderId=2,crtcId=3;
char node[]="/dev/quest-video-memory";
char* nodes[]={node,nullptr,nullptr};
drmDevice virtualDevice{};
template<class T>T* allocate(){return static_cast<T*>(calloc(1,sizeof(T)));}
int unsupported(){errno=ENOTSUP;return -1;}
}
extern "C" {
int drmGetDevices2(uint32_t,drmDevicePtr devices[],int maxDevices){
 virtualDevice.nodes=nodes;virtualDevice.available_nodes=1;virtualDevice.bustype=DRM_BUS_PLATFORM;
 if(devices&&maxDevices>0)devices[0]=&virtualDevice;
 return 1;
}
void drmFreeDevices(drmDevicePtr[],int){}
int drmSetMaster(int){errno=EACCES;return -1;}
int drmSetClientCap(int,uint64_t,uint64_t){return unsupported();}
drmModeResPtr drmModeGetResources(int){
 auto r=allocate<drmModeRes>();if(!r)return nullptr;
 r->count_crtcs=r->count_connectors=r->count_encoders=1;
 r->crtcs=&crtcId;r->connectors=&connectorId;r->encoders=&encoderId;
 r->min_width=r->min_height=1;r->max_width=r->max_height=4096;
 return r;
}
void drmModeFreeResources(drmModeResPtr p){free(p);}
drmModeConnectorPtr drmModeGetConnector(int,uint32_t id){
 auto c=allocate<drmModeConnector>();if(!c)return nullptr;
 c->connector_id=id;c->encoder_id=encoderId;c->connector_type=DRM_MODE_CONNECTOR_VIRTUAL;
 c->connection=DRM_MODE_CONNECTED;c->modes=allocate<drmModeModeInfo>();
 if(!c->modes){free(c);return nullptr;}
 c->count_modes=1;c->modes->hdisplay=1280;c->modes->vdisplay=720;c->modes->vrefresh=72;return c;
}
void drmModeFreeConnector(drmModeConnectorPtr p){if(p)free(p->modes);free(p);}
drmModeEncoderPtr drmModeGetEncoder(int,uint32_t id){
 auto e=allocate<drmModeEncoder>();if(e){e->encoder_id=id;e->crtc_id=crtcId;e->possible_crtcs=1;}return e;
}
void drmModeFreeEncoder(drmModeEncoderPtr p){free(p);}
drmModeCrtcPtr drmModeGetCrtc(int,uint32_t id){
 auto c=allocate<drmModeCrtc>();if(c){c->crtc_id=id;c->width=1280;c->height=720;c->mode_valid=1;c->mode.hdisplay=1280;c->mode.vdisplay=720;}return c;
}
void drmModeFreeCrtc(drmModeCrtcPtr p){free(p);}
int drmModeSetCrtc(int,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*,int,drmModeModeInfoPtr){return unsupported();}
drmModePlaneResPtr drmModeGetPlaneResources(int){return nullptr;}
void drmModeFreePlaneResources(drmModePlaneResPtr p){free(p);}
drmModePlanePtr drmModeGetPlane(int,uint32_t){return nullptr;}
void drmModeFreePlane(drmModePlanePtr p){free(p);}
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int,uint32_t,uint32_t){return nullptr;}
void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr p){free(p);}
drmModePropertyPtr drmModeGetProperty(int,uint32_t){return nullptr;}
void drmModeFreeProperty(drmModePropertyPtr p){free(p);}
int drmModeAddFB2(int,uint32_t,uint32_t,uint32_t,const uint32_t[4],const uint32_t[4],const uint32_t[4],uint32_t*,uint32_t){return unsupported();}
int drmModeAddFB2WithModifiers(int,uint32_t,uint32_t,uint32_t,const uint32_t[4],const uint32_t[4],const uint32_t[4],const uint64_t[4],uint32_t*,uint32_t){return unsupported();}
int drmModeRmFB(int,uint32_t){return unsupported();}
drmModeAtomicReqPtr drmModeAtomicAlloc(){return nullptr;}
void drmModeAtomicFree(drmModeAtomicReqPtr){}
int drmModeAtomicAddProperty(drmModeAtomicReqPtr,uint32_t,uint32_t,uint64_t){return unsupported();}
int drmModeAtomicCommit(int,drmModeAtomicReqPtr,uint32_t,void*){return unsupported();}
int drmIoctl(int,unsigned long request,void* data){
 if(!data){errno=EFAULT;return -1;}
 std::lock_guard<std::mutex> guard(lock);
 switch(request){
 case DRM_IOCTL_MODE_CREATE_DUMB:{
  auto b=static_cast<drm_mode_create_dumb*>(data);
  if(!b->width||!b->height||b->width>4096||b->height>4096||!b->bpp||b->bpp>64){errno=EINVAL;return -1;}
  b->pitch=b->width*((b->bpp+7)/8);b->size=uint64_t(b->pitch)*b->height;
  int fd=memfd_create("quest-video-buffer",MFD_CLOEXEC);
  if(fd<0)return -1;
  if(ftruncate(fd,b->size)){close(fd);return -1;}
  b->handle=nextHandle++;buffers[b->handle]=fd;
  fprintf(stderr,"Quest video: allocated buffer %u size=%llu pitch=%u\n",b->handle,(unsigned long long)b->size,b->pitch);
  return 0;
 }
 case DRM_IOCTL_PRIME_HANDLE_TO_FD:{
  auto p=static_cast<drm_prime_handle*>(data);auto i=buffers.find(p->handle);
  if(i==buffers.end()){errno=ENOENT;return -1;}p->fd=fcntl(i->second,F_DUPFD_CLOEXEC,0);return p->fd<0?-1:0;
 }
 case DRM_IOCTL_MODE_DESTROY_DUMB:{
  auto p=static_cast<drm_mode_destroy_dumb*>(data);auto i=buffers.find(p->handle);
  if(i==buffers.end()){errno=ENOENT;return -1;}close(i->second);buffers.erase(i);return 0;
 }
 default:return unsupported();
 }
}
}
