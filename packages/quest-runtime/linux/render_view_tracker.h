#pragma once
#include "render_view.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <vector>
#include <unordered_set>

namespace quest {
struct RenderViews {
 QuestGpuView views[2]{};uint64_t stamp[2]{};
 QuestGpuView pending{};uint64_t pendingStamp=0;
 bool complete()const{return stamp[0]&&stamp[1];}
 void candidate(const QuestGpuView& v,uint64_t time){
  if(!pendingStamp){pending=v;pendingStamp=time;return;}
  const auto* q=pending.orientation;float right[]={1-2*(q[1]*q[1]+q[2]*q[2]),2*(q[0]*q[1]+q[3]*q[2]),2*(q[0]*q[2]-q[3]*q[1])};
  float along=0,distance=0;for(int i=0;i<3;i++){float delta=v.position[i]-pending.position[i];along+=right[i]*delta;distance+=delta*delta;}
  // Equal per-eye FOVs cannot identify handedness. The physical stereo
  // baseline can: right eye lies along the camera's local +X axis.
  if(distance<.0004f){if(time>=pendingStamp){pending=v;pendingStamp=time;}return;}
  if(distance>.0144f||along*along<.81f*distance)return;
  int first=along>0?0:1;views[first]=pending;stamp[first]=pendingStamp;views[1-first]=v;stamp[1-first]=time;pendingStamp=0;
 }
 void merge(const RenderViews& other){for(int i=0;i<2;i++)if(other.stamp[i]>stamp[i]){views[i]=other.views[i];stamp[i]=other.stamp[i];}if(other.pendingStamp)candidate(other.pending,other.pendingStamp);}
};
// Observe CPU-written Frame camera constants; never modify GPU work or waits.
// Binary semaphore dependencies carry the cameras to the presented image.
class RenderViewTracker {
 struct Mapping{const uint8_t* pointer;VkDeviceSize offset,size;};
 struct Buffer{VkDeviceMemory memory;VkDeviceSize offset;};
 struct Command{bool geometry=false;VkDescriptorSet set=VK_NULL_HANDLE;VkDescriptorBufferInfo eyes[2]{};};
 std::mutex lock;uint64_t serial=0;
 float fov[2][4]{};bool haveFov=false;
 std::unordered_map<VkDeviceMemory,VkDeviceSize> sizes;
 std::unordered_map<VkDeviceMemory,Mapping> memory;
 std::unordered_map<VkBuffer,Buffer> buffers;
 std::unordered_map<VkDescriptorSet,VkDescriptorBufferInfo> sets;
 std::unordered_map<VkDescriptorPool,std::unordered_set<VkDescriptorSet>> pools;
 std::unordered_map<VkPipeline,bool> pipelines;
 std::unordered_map<VkCommandBuffer,Command> commands;
 std::unordered_map<VkSemaphore,RenderViews> signals;
 std::unordered_map<VkQueue,RenderViews> queues;
 bool read(const VkDescriptorBufferInfo& info,QuestGpuView& view){
  auto b=buffers.find(info.buffer);if(b==buffers.end()||info.range<192)return false;
  auto m=memory.find(b->second.memory);if(m==memory.end()||info.offset>UINT64_MAX-b->second.offset)return false;
  auto offset=b->second.offset+info.offset;if(offset<m->second.offset)return false;
  offset-=m->second.offset;if(offset>m->second.size||192>m->second.size-offset)return false;
  float data[48];memcpy(data,m->second.pointer+offset,sizeof(data));return renderView(data,view);
 }
 int eye(const QuestGpuView& view)const{
  if(!haveFov)return -1;
  float error[2]{};for(int i=0;i<2;i++)for(int j=0;j<4;j++)error[i]+=std::fabs(view.fov[j]-fov[i][j]);
  int i=error[0]<error[1]?0:1;
  if(error[0]<.002f&&error[1]<.002f&&std::fabs(error[0]-error[1])<=.0001f)return 2;
  return error[i]<.002f?i:-1;
 }
public:
 void reset(){std::lock_guard<std::mutex> g(lock);sizes.clear();memory.clear();buffers.clear();sets.clear();pools.clear();pipelines.clear();commands.clear();signals.clear();queues.clear();haveFov=false;}
 void expected(const QuestGpuPresent& sample){if(!sample.pose_sequence)return;std::lock_guard<std::mutex> g(lock);for(int i=0;i<2;i++)memcpy(fov[i],sample.views[i].fov,sizeof(fov[i]));haveFov=true;}
 void allocate(VkDeviceMemory m,VkDeviceSize size){std::lock_guard<std::mutex> g(lock);sizes[m]=size;}
 void free(VkDeviceMemory m){std::lock_guard<std::mutex> g(lock);memory.erase(m);sizes.erase(m);for(auto i=buffers.begin();i!=buffers.end();)if(i->second.memory==m)i=buffers.erase(i);else ++i;}
 void map(VkDeviceMemory m,VkDeviceSize offset,VkDeviceSize size,const void* pointer){std::lock_guard<std::mutex> g(lock);auto s=sizes.find(m);if(size==VK_WHOLE_SIZE){if(s==sizes.end()||offset>s->second)return;size=s->second-offset;}memory[m]={static_cast<const uint8_t*>(pointer),offset,size};}
 void unmap(VkDeviceMemory m){std::lock_guard<std::mutex> g(lock);memory.erase(m);}
 void bind(VkBuffer b,VkDeviceMemory m,VkDeviceSize offset){std::lock_guard<std::mutex> g(lock);buffers[b]={m,offset};}
 void destroyBuffer(VkBuffer b){std::lock_guard<std::mutex> g(lock);buffers.erase(b);}
 void pipeline(VkPipeline p,bool geometry){std::lock_guard<std::mutex> g(lock);pipelines[p]=geometry;}
 void destroyPipeline(VkPipeline p){std::lock_guard<std::mutex> g(lock);pipelines.erase(p);}
 void begin(VkCommandBuffer c){std::lock_guard<std::mutex> g(lock);commands.erase(c);}
 void bindPipeline(VkCommandBuffer c,VkPipeline p){std::lock_guard<std::mutex> g(lock);auto i=pipelines.find(p);commands[c].geometry=i!=pipelines.end()&&i->second;}
 void bindSet(VkCommandBuffer c,VkDescriptorSet s,bool dynamic){std::lock_guard<std::mutex> g(lock);commands[c].set=dynamic?VK_NULL_HANDLE:s;}
 void write(uint32_t count,const VkWriteDescriptorSet* writes,uint32_t nCopies,const VkCopyDescriptorSet* copies){
  std::lock_guard<std::mutex> g(lock);
  for(uint32_t i=0;i<count;i++)if(writes[i].dstBinding==0&&writes[i].descriptorCount){
   sets.erase(writes[i].dstSet);
   if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&writes[i].dstArrayElement==0)sets[writes[i].dstSet]=writes[i].pBufferInfo[0];
  }
  for(uint32_t i=0;i<nCopies;i++)if(copies[i].dstBinding==0){auto s=sets.find(copies[i].srcSet);if(copies[i].srcBinding==0&&s!=sets.end())sets[copies[i].dstSet]=s->second;else sets.erase(copies[i].dstSet);}
 }
 void freeSet(VkDescriptorSet s){std::lock_guard<std::mutex> g(lock);sets.erase(s);for(auto& pool:pools)pool.second.erase(s);}
 void allocateSets(VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* values){std::lock_guard<std::mutex> g(lock);auto& p=pools[pool];p.insert(values,values+count);}
 void resetPool(VkDescriptorPool pool){std::lock_guard<std::mutex> g(lock);auto p=pools.find(pool);if(p!=pools.end()){for(auto s:p->second)sets.erase(s);pools.erase(p);}}
 void draw(VkCommandBuffer c){
  std::lock_guard<std::mutex> g(lock);auto i=commands.find(c);if(i==commands.end()||!i->second.geometry)return;
  auto& command=i->second;if(command.eyes[0].buffer&&command.eyes[1].buffer)return;
  auto s=sets.find(command.set);if(s==sets.end())return;
  QuestGpuView v{};if(!read(s->second,v))return;int e=eye(v);
  if(e==2){
   if(!command.eyes[0].buffer){command.eyes[0]=s->second;return;}
   QuestGpuView first{};if(!read(command.eyes[0],first))return;
   float distance=0;for(int i=0;i<3;i++)distance+=(v.position[i]-first.position[i])*(v.position[i]-first.position[i]);
   if(distance>.0004f)command.eyes[1]=s->second;
  }else if(e>=0)command.eyes[e]=s->second;
 }
 void submit(VkQueue queue,uint32_t count,const VkSubmitInfo* info){
  std::lock_guard<std::mutex> g(lock);
  for(uint32_t i=0;i<count;i++){
   RenderViews frame=queues[queue];uint64_t stamp=++serial;
   for(uint32_t j=0;j<info[i].waitSemaphoreCount;j++){auto s=signals.find(info[i].pWaitSemaphores[j]);if(s!=signals.end())frame.merge(s->second);}
   for(uint32_t j=0;j<info[i].commandBufferCount;j++){
    auto c=commands.find(info[i].pCommandBuffers[j]);if(c==commands.end())continue;
    for(int e=0;e<2;e++){QuestGpuView v{};if(read(c->second.eyes[e],v)){int index=eye(v);if(index==2)frame.candidate(v,stamp);else if(index>=0){frame.views[index]=v;frame.stamp[index]=stamp;}}}
   }
   queues[queue]=frame;for(uint32_t j=0;j<info[i].signalSemaphoreCount;j++)signals[info[i].pSignalSemaphores[j]]=frame;
  }
 }
 void acquired(VkSemaphore s){if(!s)return;std::lock_guard<std::mutex> g(lock);signals.erase(s);}
 void destroySemaphore(VkSemaphore s){std::lock_guard<std::mutex> g(lock);signals.erase(s);}
 RenderViews present(VkQueue queue,uint32_t count,const VkSemaphore* waits){
  std::lock_guard<std::mutex> g(lock);RenderViews result;
  if(!count){auto q=queues.find(queue);if(q!=queues.end())result=q->second;}
  for(uint32_t i=0;i<count;i++){auto s=signals.find(waits[i]);if(s!=signals.end())result.merge(s->second);}
  queues.erase(queue);for(uint32_t i=0;i<count;i++)signals.erase(waits[i]);
  return result;
 }
};
}
