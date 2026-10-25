#include "video_bridge.h"
#include "video_protocol.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <vector>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"StearlightVideo",__VA_ARGS__)
namespace {
bool transfer(int fd,void* data,size_t bytes,bool sendData){
 auto p=static_cast<unsigned char*>(data);while(bytes){ssize_t n=sendData?send(fd,p,bytes,MSG_NOSIGNAL):recv(fd,p,bytes,0);if(n<0&&errno==EINTR)continue;if(n<=0)return false;p+=n;bytes-=n;}return true;
}
struct Decoder {
 AMediaCodec* codec=nullptr;int width=0,height=0,stride=0,slice=0,color=0;
 std::map<int64_t,uint64_t> frameTags;int64_t lastCodecPts=0;
 bool decoded=false;
 void* shared=MAP_FAILED;size_t capacity=0;
 ~Decoder(){reset();if(shared!=MAP_FAILED)munmap(shared,capacity);}
 bool outputMemory(int fd){
  struct stat info{};
  if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_size<=0||uint64_t(info.st_size)>QUEST_VIDEO_MAX_FRAME)return false;
  void* memory=mmap(nullptr,info.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(memory==MAP_FAILED)return false;
  if(shared!=MAP_FAILED)munmap(shared,capacity);shared=memory;capacity=info.st_size;return true;
 }
 void reset(){frameTags.clear();lastCodecPts=0;decoded=false;if(codec){AMediaCodec_stop(codec);AMediaCodec_delete(codec);codec=nullptr;}}
 bool configure(const QuestVideoRequest& r){
  reset();if(r.width<128||r.height<128||r.width>QUEST_VIDEO_MAX_DIMENSION||r.height>QUEST_VIDEO_MAX_DIMENSION||uint64_t(r.width)*r.height>QUEST_VIDEO_MAX_PIXELS||(r.codec!=4&&r.codec!=5))return false;
  codec=AMediaCodec_createDecoderByType(r.codec==4?"video/avc":"video/hevc");if(!codec)return false;

  AMediaFormat* f=AMediaFormat_new();AMediaFormat_setString(f,AMEDIAFORMAT_KEY_MIME,r.codec==4?"video/avc":"video/hevc");
  AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_WIDTH,r.width);AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_HEIGHT,r.height);
  AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_COLOR_FORMAT,21);AMediaFormat_setInt32(f,"low-latency",1);
  AMediaFormat_setInt32(f,AMEDIAFORMAT_KEY_MAX_INPUT_SIZE,QUEST_VIDEO_MAX_PACKET);
  auto result=AMediaCodec_configure(codec,f,nullptr,nullptr,0);AMediaFormat_delete(f);
  if(result!=AMEDIA_OK||AMediaCodec_start(codec)!=AMEDIA_OK){LOG("MediaCodec configure failed %d",result);reset();return false;}
  width=r.width;height=r.height;stride=width;slice=height;color=21;char* name=nullptr;
  if(AMediaCodec_getName(codec,&name)==AMEDIA_OK){LOG("Streaming decoder %s: %dx%d",name,width,height);AMediaCodec_releaseName(codec,name);}return true;
 }
 bool submit(const QuestVideoRequest& r,const std::vector<unsigned char>& packet,QuestVideoResponse& response,std::vector<unsigned char>& frame){
  if(!codec)return false;
  // The first OMX input allocation can take longer during a loaded launch.
  // Allow startup to finish below the player's 800 ms reset threshold.
  auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(decoded?100:500);
  ssize_t index;
  // MediaCodec stops accepting input when its output slots are full. Waiting
  // for input on this same thread cannot free those slots: drain output while
  // retrying instead of turning backpressure into a decoder/stream reset.
  while((index=AMediaCodec_dequeueInputBuffer(codec,0))<0){
   if(index!=AMEDIACODEC_INFO_TRY_AGAIN_LATER||!output(response,frame)||std::chrono::steady_clock::now()>=deadline){
    LOG("Streaming input stalled: result=%zd",index);return false;
   }
  }
  size_t size=0;auto target=AMediaCodec_getInputBuffer(codec,index,&size);
  if(!target||size<packet.size())return false;memcpy(target,packet.data(),packet.size());
  // V4L2 timestamps identify frames; they can be spaced a second apart.
  // Give Android a monotonic codec clock and restore the exact tag on output.
  int64_t now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  int64_t pts=std::max(lastCodecPts+1,now);
  if(AMediaCodec_queueInputBuffer(codec,index,0,packet.size(),pts,0)!=AMEDIA_OK)return false;
  lastCodecPts=pts;frameTags.emplace(pts,r.timestamp);while(frameTags.size()>256)frameTags.erase(frameTags.begin());return true;
 }
 bool output(QuestVideoResponse& response,std::vector<unsigned char>& frame){
  AMediaCodecBufferInfo info{};ssize_t index=AMediaCodec_dequeueOutputBuffer(codec,&info,5000);
  if(index==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED){
   auto f=AMediaCodec_getOutputFormat(codec);LOG("Streaming output: %s",AMediaFormat_toString(f));
   AMediaFormat_getInt32(f,AMEDIAFORMAT_KEY_WIDTH,&width);AMediaFormat_getInt32(f,AMEDIAFORMAT_KEY_HEIGHT,&height);
   stride=width;slice=height;AMediaFormat_getInt32(f,"stride",&stride);AMediaFormat_getInt32(f,"slice-height",&slice);
   AMediaFormat_getInt32(f,AMEDIAFORMAT_KEY_COLOR_FORMAT,&color);AMediaFormat_delete(f);
   if(width<=0||height<=0||width>QUEST_VIDEO_MAX_DIMENSION||height>QUEST_VIDEO_MAX_DIMENSION||uint64_t(width)*height>QUEST_VIDEO_MAX_PIXELS||(width|height)&1)return false;
   response.status=QUEST_VIDEO_FORMAT_CHANGED;response.width=width;response.height=height;response.bytes=0;response.timestamp=0;return true;
  }
  if(index<0)return true;
  auto tag=frameTags.find(info.presentationTimeUs);
  if(tag==frameTags.end()){AMediaCodec_releaseOutputBuffer(codec,index,false);return true;}
  uint64_t originalTag=tag->second;frameTags.erase(tag);
  size_t size=0;auto data=AMediaCodec_getOutputBuffer(codec,index,&size);
  bool valid=width>0&&height>0&&width<=QUEST_VIDEO_MAX_DIMENSION&&height<=QUEST_VIDEO_MAX_DIMENSION&&uint64_t(width)*height<=QUEST_VIDEO_MAX_PIXELS&&!(width&1)&&!(height&1)&&stride>=width&&slice>=height;
  size_t luma=valid?size_t(stride)*slice:0;
  valid=valid&&data&&info.offset>=0&&size>=size_t(info.offset)+luma*3/2&&(color==21||color==19||color==0x7fa30c04);
  if(valid){
   size_t bytes=size_t(width)*height*3/2;
   if(shared!=MAP_FAILED&&bytes>capacity){AMediaCodec_releaseOutputBuffer(codec,index,false);return false;}
   data+=info.offset;if(shared==MAP_FAILED)frame.resize(bytes);
   auto output=shared==MAP_FAILED?frame.data():static_cast<unsigned char*>(shared);
   for(int y=0;y<height;y++)memcpy(output+size_t(y)*width,data+size_t(y)*stride,width);
   auto uv=output+size_t(width)*height;
   for(int y=0;y<height/2;y++){
    if(color!=19)memcpy(uv+size_t(y)*width,data+luma+size_t(y)*stride,width);
    else for(int x=0;x<width/2;x++){uv[size_t(y)*width+2*x]=data[luma+size_t(y)*(stride/2)+x];uv[size_t(y)*width+2*x+1]=data[luma+luma/4+size_t(y)*(stride/2)+x];}
   }
   decoded=true;response.status=0;response.width=width;response.height=height;response.bytes=bytes;response.color=21;response.timestamp=originalTag;
  }else LOG("Unsupported streaming output: color=%d size=%zu offset=%d stride=%d slice=%d",color,size,info.offset,stride,slice);
  AMediaCodec_releaseOutputBuffer(codec,index,false);return valid;
 }
};
}
void VideoBridge::initialize(const std::string& directory){
 path=directory+"/video.sock";if(path.size()>=sizeof(sockaddr_un::sun_path))throw std::runtime_error("video socket path");
 server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,path.c_str());unlink(path.c_str());
 if(server<0||bind(server,reinterpret_cast<sockaddr*>(&a),sizeof(a))||listen(server,2))throw std::runtime_error("video socket");chmod(path.c_str(),0600);
 worker=std::thread(&VideoBridge::serve,this);LOG("Android video decoder bridge ready");
}
void VideoBridge::serve(){
 while(!stopping){pollfd event{server,POLLIN,0};if(poll(&event,1,200)<=0)continue;
  int connection=accept4(server,nullptr,nullptr,SOCK_CLOEXEC);if(connection<0)continue;
  sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[](auto& s){return s.wait_for(std::chrono::seconds(0))==std::future_status::ready;}),sessions.end());
  {std::lock_guard<std::mutex> guard(lock);if(stopping||clients.size()>=4){close(connection);continue;}clients.insert(connection);}
  sessions.push_back(std::async(std::launch::async,&VideoBridge::serveClient,this,connection));
 }
 for(auto& session:sessions)session.wait();sessions.clear();
}
void VideoBridge::serveClient(int connection){
  Decoder decoder;std::vector<unsigned char> packet,frame;unsigned frames=0;
  while(!stopping){
   QuestVideoRequest r{};int outputFd=-1;char ancillary[CMSG_SPACE(sizeof(int))]{};
   iovec io{&r,sizeof(r)};msghdr message{};message.msg_iov=&io;message.msg_iovlen=1;message.msg_control=ancillary;message.msg_controllen=sizeof(ancillary);
   ssize_t received;do received=recvmsg(connection,&message,MSG_WAITALL|MSG_CMSG_CLOEXEC);while(received<0&&errno==EINTR);
   if(received<=0)break;
   for(cmsghdr* header=CMSG_FIRSTHDR(&message);header;header=CMSG_NXTHDR(&message,header))if(header->cmsg_level==SOL_SOCKET&&header->cmsg_type==SCM_RIGHTS){
    size_t count=(header->cmsg_len-CMSG_LEN(0))/sizeof(int);auto fds=reinterpret_cast<int*>(CMSG_DATA(header));
    for(size_t i=0;i<count;i++){if(outputFd<0)outputFd=fds[i];else close(fds[i]);}
   }
   if((message.msg_flags&MSG_CTRUNC)||(size_t(received)<sizeof(r)&&!transfer(connection,reinterpret_cast<char*>(&r)+received,sizeof(r)-received,false))){if(outputFd>=0)close(outputFd);break;}
   bool outputMemory=r.operation==QUEST_VIDEO_SHARED_OUTPUT&&!r.bytes&&decoder.outputMemory(outputFd);
   if(outputFd>=0)close(outputFd);
   if(r.magic!=QUEST_VIDEO_MAGIC||r.bytes>QUEST_VIDEO_MAX_PACKET)break;
   packet.resize(r.bytes);if(r.bytes&&!transfer(connection,packet.data(),r.bytes,false))break;
   QuestVideoResponse response{};response.magic=QUEST_VIDEO_MAGIC;response.status=1;frame.clear();
   bool valid=false;
   if(r.operation==QUEST_VIDEO_SHARED_OUTPUT)valid=outputMemory;
   else if(r.operation==QUEST_VIDEO_CONFIGURE)valid=decoder.configure(r);
   else if(decoder.codec&&(r.operation==QUEST_VIDEO_PACKET||r.operation==QUEST_VIDEO_POLL)){
    valid=r.operation==QUEST_VIDEO_POLL||decoder.submit(r,packet,response,frame);
    // submit may already have drained a frame to make room for this packet.
    if(valid&&response.status==QUEST_VIDEO_ACCEPTED)valid=decoder.output(response,frame);
   }
   if(!valid)response.status=-1;
   if(!transfer(connection,&response,sizeof(response),true)||(response.bytes&&decoder.shared==MAP_FAILED&&!transfer(connection,frame.data(),response.bytes,true)))break;
   if(response.status==0&&++frames%300==1)LOG("Streaming decoded frames=%u %ux%u",frames,response.width,response.height);
  }
  {std::lock_guard<std::mutex> guard(lock);close(connection);clients.erase(connection);}
  LOG("Streaming decoder connection closed; frames=%u",frames);
}
void VideoBridge::shutdown(){
 stopping=true;{std::lock_guard<std::mutex> guard(lock);for(int client:clients)::shutdown(client,SHUT_RDWR);}
 if(worker.joinable())worker.join();if(server>=0){close(server);server=-1;unlink(path.c_str());}
}
