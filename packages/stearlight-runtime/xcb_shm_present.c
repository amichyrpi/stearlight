#define _GNU_SOURCE
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* CPU-only Venus presentation cannot import host memory as a Vulkan image.
 * Keep its real GPU fence and X-server completion boundary, but transfer the
 * completed image through MIT-SHM instead of copying it through a socket. */
#define POOL_BYTES (32u * 1024u * 1024u)
#define CONNECTIONS 4
struct pool { xcb_connection_t *connection; xcb_shm_seg_t segment; void *memory; uint32_t offset; int available; };
static struct pool pools[CONNECTIONS];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static int enabled;
static __typeof__(&xcb_put_image) real_put;
static __typeof__(&xcb_put_image_checked) real_put_checked;
static __typeof__(&xcb_disconnect) real_disconnect;
static __typeof__(&xcb_shm_attach_checked) attach_checked;
static __typeof__(&xcb_shm_detach) detach_segment;
static __typeof__(&xcb_shm_put_image) put_shared;
static __typeof__(&xcb_shm_put_image_checked) put_shared_checked;
static __typeof__(&xcb_get_input_focus) focus;
static __typeof__(&xcb_get_input_focus_reply) focus_reply;
static __typeof__(&xcb_request_check) request_check;
static __typeof__(&xcb_generate_id) generate_id;
static __typeof__(&xcb_get_extension_data) extension_data;
static xcb_extension_t *shm_extension;
static void initialize(void) {
 void *xcb=dlopen("libxcb.so.1",RTLD_NOW|RTLD_LOCAL);
 real_put=xcb?dlsym(xcb,"xcb_put_image"):NULL;
 real_put_checked=xcb?dlsym(xcb,"xcb_put_image_checked"):NULL;
 real_disconnect=xcb?dlsym(xcb,"xcb_disconnect"):NULL;
 const char *setting=getenv("STEARLIGHT_XCB_SHM_PRESENT");
 if (!setting || strcmp(setting,"1")) return;
 void *shm=dlopen("libxcb-shm.so.0",RTLD_NOW|RTLD_LOCAL);
 if (!xcb || !shm) return;
 attach_checked=dlsym(shm,"xcb_shm_attach_checked");
 detach_segment=dlsym(shm,"xcb_shm_detach");
 put_shared=dlsym(shm,"xcb_shm_put_image");
 put_shared_checked=dlsym(shm,"xcb_shm_put_image_checked");
 shm_extension=dlsym(shm,"xcb_shm_id");
 focus=dlsym(xcb,"xcb_get_input_focus"); focus_reply=dlsym(xcb,"xcb_get_input_focus_reply");
 request_check=dlsym(xcb,"xcb_request_check"); generate_id=dlsym(xcb,"xcb_generate_id");
 extension_data=dlsym(xcb,"xcb_get_extension_data");
 enabled=attach_checked && detach_segment && put_shared && put_shared_checked && shm_extension && focus && focus_reply && request_check && generate_id && extension_data;
}
static struct pool *get_pool(xcb_connection_t *connection) {
 struct pool *pool=NULL;
 for (unsigned n=0;n<CONNECTIONS;n++) {
  if (pools[n].connection==connection) return &pools[n];
  if (!pools[n].connection && !pool) pool=&pools[n];
 }
 if (!pool) return NULL;
 pool->connection=connection;
 const xcb_query_extension_reply_t *extension=extension_data(connection,shm_extension);
 if (!extension || !extension->present) return pool;
 int id=shmget(IPC_PRIVATE,POOL_BYTES,IPC_CREAT|0600);
 if (id<0) return pool;
 void *memory=shmat(id,NULL,0);
 if (memory==(void *)-1) { shmctl(id,IPC_RMID,NULL); return pool; }
 xcb_shm_seg_t segment=generate_id(connection);
 xcb_generic_error_t *error=request_check(connection,attach_checked(connection,segment,id,1));
 shmctl(id,IPC_RMID,NULL);
 if (error) { free(error); shmdt(memory); return pool; }
 pool->segment=segment; pool->memory=memory; pool->available=1;
 fprintf(stderr,"STEARLIGHT XCB SHM: CPU presentation pool ready (%u bytes)\n",POOL_BYTES);
 return pool;
}
static int upload(xcb_connection_t *connection,uint8_t format,xcb_drawable_t drawable,xcb_gcontext_t gc,
                  uint16_t width,uint16_t height,int16_t x,int16_t y,uint8_t left_pad,uint8_t depth,
                  uint32_t length,const uint8_t *data,int checked,xcb_void_cookie_t *cookie) {
 if (!enabled || format!=XCB_IMAGE_FORMAT_Z_PIXMAP || left_pad || (depth!=24 && depth!=32) ||
     !width || !height || !data || (uint64_t)width*height*4!=length || length>POOL_BYTES) return 0;
 pthread_mutex_lock(&lock);
 struct pool *pool=get_pool(connection);
 if (!pool || !pool->available) { pthread_mutex_unlock(&lock); return 0; }
 uint32_t allocation=(length+63u)&~63u;
 if (allocation>POOL_BYTES-pool->offset) {
  /* Never overwrite bytes until every previous upload was consumed. */
  xcb_generic_error_t *error=NULL;
  void *reply=focus_reply(connection,focus(connection),&error);
  if (!reply || error) { free(reply); free(error); pool->available=0; pthread_mutex_unlock(&lock); return 0; }
  free(reply); pool->offset=0;
 }
 uint32_t offset=pool->offset;
 memcpy((uint8_t *)pool->memory+offset,data,length);
 *cookie=(checked?put_shared_checked:put_shared)(connection,drawable,gc,width,height,0,0,width,height,x,y,depth,format,0,pool->segment,offset);
 pool->offset+=allocation;
 pthread_mutex_unlock(&lock);
 return 1;
}
xcb_void_cookie_t xcb_put_image(xcb_connection_t *connection,uint8_t format,xcb_drawable_t drawable,xcb_gcontext_t gc,
                              uint16_t width,uint16_t height,int16_t x,int16_t y,uint8_t left_pad,uint8_t depth,
                              uint32_t length,const uint8_t *data) {
 pthread_once(&once,initialize);
 xcb_void_cookie_t cookie={0};
 if (upload(connection,format,drawable,gc,width,height,x,y,left_pad,depth,length,data,0,&cookie)) return cookie;
 return real_put(connection,format,drawable,gc,width,height,x,y,left_pad,depth,length,data);
}
xcb_void_cookie_t xcb_put_image_checked(xcb_connection_t *connection,uint8_t format,xcb_drawable_t drawable,xcb_gcontext_t gc,
                                      uint16_t width,uint16_t height,int16_t x,int16_t y,uint8_t left_pad,uint8_t depth,
                                      uint32_t length,const uint8_t *data) {
 pthread_once(&once,initialize);
 xcb_void_cookie_t cookie={0};
 if (upload(connection,format,drawable,gc,width,height,x,y,left_pad,depth,length,data,1,&cookie)) return cookie;
 return real_put_checked(connection,format,drawable,gc,width,height,x,y,left_pad,depth,length,data);
}
void xcb_disconnect(xcb_connection_t *connection) {
 pthread_once(&once,initialize);
 pthread_mutex_lock(&lock);
 for (unsigned n=0;n<CONNECTIONS;n++) if (pools[n].connection==connection) {
  if (pools[n].memory) { detach_segment(connection,pools[n].segment); shmdt(pools[n].memory); }
  memset(&pools[n],0,sizeof(pools[n]));
 }
 pthread_mutex_unlock(&lock);
 real_disconnect(connection);
}
