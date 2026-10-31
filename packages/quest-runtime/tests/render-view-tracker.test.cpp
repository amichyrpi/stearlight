#include "../linux/render_view_tracker.h"
#include <cassert>
#include <cstdio>
template<typename T>static T h(uintptr_t n){return reinterpret_cast<T>(n);}
int main(){
 quest::RenderViewTracker tracker;
 auto memory=h<VkDeviceMemory>(1);auto left=h<VkBuffer>(2),right=h<VkBuffer>(3);
 auto ls=h<VkDescriptorSet>(4),rs=h<VkDescriptorSet>(5);auto pipeline=h<VkPipeline>(6);
 auto command=h<VkCommandBuffer>(7);auto renderQueue=h<VkQueue>(8),copyQueue=h<VkQueue>(9);
 auto ready=h<VkSemaphore>(10),present=h<VkSemaphore>(11);
 float data[96]{};QuestGpuPresent expected{};expected.pose_sequence=1;
 for(int eye=0;eye<2;eye++){
  auto* d=data+48*eye;for(int i=0;i<4;i++)d[i*5]=d[16+i*5]=1;
  float l=eye?-1.f:-1.15f,r=eye?1.15f:1.f;
  auto* p=d+32;p[0]=2/(r-l);p[2]=(r+l)/(r-l);p[5]=2/2.3f;p[6]=-.08f/2.3f;p[10]=-1.001f;p[11]=-.1f;p[14]=-1;
  assert(quest::renderView(d,expected.views[eye]));
 }
 tracker.expected(expected);tracker.allocate(memory,sizeof(data));tracker.map(memory,0,VK_WHOLE_SIZE,data);
 tracker.bind(left,memory,0);tracker.bind(right,memory,48*sizeof(float));
 VkDescriptorBufferInfo buffers[2]={{left,0,192},{right,0,192}};
 VkWriteDescriptorSet writes[2]{};
 for(int i=0;i<2;i++){writes[i].dstSet=i?rs:ls;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[i].pBufferInfo=buffers+i;}
 tracker.write(2,writes,0,nullptr);tracker.pipeline(pipeline,true);tracker.begin(command);tracker.bindPipeline(command,pipeline);
 tracker.bindSet(command,ls,false);tracker.draw(command);tracker.bindSet(command,rs,false);tracker.draw(command);
 // Values are read at submission, after command recording has finished.
 data[19]=-1;data[48+19]=-2;
 VkSubmitInfo draw{VK_STRUCTURE_TYPE_SUBMIT_INFO};draw.commandBufferCount=1;draw.pCommandBuffers=&command;draw.signalSemaphoreCount=1;draw.pSignalSemaphores=&ready;
 tracker.submit(renderQueue,1,&draw);
 VkSubmitInfo copy{VK_STRUCTURE_TYPE_SUBMIT_INFO};copy.waitSemaphoreCount=1;copy.pWaitSemaphores=&ready;copy.signalSemaphoreCount=1;copy.pSignalSemaphores=&present;
 tracker.submit(copyQueue,1,&copy);
 // A newer render must not change the older image's presentation metadata.
 data[19]=-10;data[48+19]=-20;tracker.submit(renderQueue,1,&draw);
 auto frame=tracker.present(copyQueue,1,&present);assert(frame.complete());assert(frame.views[0].position[0]==1&&frame.views[1].position[0]==2);
 assert(!tracker.present(copyQueue,1,&present).complete());
 tracker.acquired(ready);assert(!tracker.present(renderQueue,1,&ready).complete());
 tracker.unmap(memory);tracker.submit(renderQueue,1,&draw);tracker.destroySemaphore(ready);tracker.destroyPipeline(pipeline);tracker.begin(command);tracker.free(memory);tracker.destroyBuffer(left);tracker.destroyBuffer(right);tracker.freeSet(ls);tracker.freeSet(rs);
 tracker.reset();tracker.expected(expected);tracker.allocate(memory,sizeof(data));tracker.map(memory,0,VK_WHOLE_SIZE,data);tracker.bind(left,memory,0);tracker.bind(right,memory,48*sizeof(float));tracker.write(2,writes,0,nullptr);tracker.pipeline(pipeline,true);
 auto record=[&](bool dynamic){tracker.begin(command);tracker.bindPipeline(command,pipeline);tracker.bindSet(command,ls,dynamic);tracker.draw(command);tracker.bindSet(command,rs,dynamic);tracker.draw(command);};
 record(true);tracker.submit(renderQueue,1,&draw);assert(!tracker.present(renderQueue,1,&ready).complete());
 // Freeing mapped storage before submission must not read dangling pointers.
 record(false);tracker.free(memory);tracker.submit(renderQueue,1,&draw);assert(!tracker.present(renderQueue,1,&ready).complete());
 tracker.reset();auto ambiguous=expected;memcpy(ambiguous.views[1].fov,ambiguous.views[0].fov,sizeof(ambiguous.views[0].fov));tracker.expected(ambiguous);
 tracker.allocate(memory,sizeof(data));tracker.map(memory,0,VK_WHOLE_SIZE,data);tracker.bind(left,memory,0);tracker.bind(right,memory,48*sizeof(float));tracker.write(2,writes,0,nullptr);tracker.pipeline(pipeline,true);
 record(false);tracker.submit(renderQueue,1,&draw);assert(!tracker.present(renderQueue,1,&ready).complete());
 // Identical per-eye FOVs use the measured stereo baseline, including when
 // the right-eye command is submitted before the left-eye command.
 tracker.reset();tracker.expected(ambiguous);memcpy(data+48+32,data+32,16*sizeof(float));data[19]=.0315f;data[48+19]=-.0315f;
 tracker.allocate(memory,sizeof(data));tracker.map(memory,0,VK_WHOLE_SIZE,data);tracker.bind(left,memory,0);tracker.bind(right,memory,48*sizeof(float));tracker.write(2,writes,0,nullptr);tracker.pipeline(pipeline,true);
 tracker.begin(command);tracker.bindPipeline(command,pipeline);tracker.bindSet(command,rs,false);tracker.draw(command);tracker.bindSet(command,ls,false);tracker.draw(command);tracker.submit(renderQueue,1,&draw);
 frame=tracker.present(renderQueue,1,&ready);assert(frame.complete());assert(frame.views[0].position[0]<0&&frame.views[1].position[0]>0);
 quest::RenderViews yawed;QuestGpuView a{},b{};a.orientation[1]=b.orientation[1]=std::sqrt(.5f);a.orientation[3]=b.orientation[3]=std::sqrt(.5f);a.position[2]=.0315f;b.position[2]=-.0315f;
 yawed.candidate(b,1);yawed.candidate(a,1);assert(yawed.complete());assert(yawed.views[0].position[2]>0&&yawed.views[1].position[2]<0);
 puts("Rendered views: submit-time sampling, cross-queue semaphore propagation, pipelined frame isolation and consumed semaphore invalidation pass");
}
