"""Run the production WSI event waits against a socketpair, without a GPU."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'linux/quest_wsi.cpp').read_text()
events = source[source.index(' bool eventWaiting='):source.index(' const QuestTracking* tracking=')]
acquire = source[source.index('VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR('):source.index('VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(')]
wait = source[source.index('VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresentKHR('):source.index('VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(')]

with tempfile.TemporaryDirectory() as folder:
    test, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
    test.write_text(r'''
#include "gpu_bridge.h"
#include "gpu_frames.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cerrno>
#include <future>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define VKAPI_ATTR
#define VKAPI_CALL
using VkDevice=uint64_t;using VkSwapchainKHR=uintptr_t;
using VkSemaphore=uint64_t;using VkFence=uint64_t;using VkQueue=uint64_t;
enum VkResult{VK_SUCCESS,VK_TIMEOUT,VK_NOT_READY,VK_ERROR_SURFACE_LOST_KHR};
constexpr int VK_STRUCTURE_TYPE_SUBMIT_INFO=1;
struct VkSubmitInfo{int type;unsigned signalSemaphoreCount=0;const VkSemaphore* pSignalSemaphores=nullptr;unsigned commandBufferCount=0;const uint64_t* pCommandBuffers=nullptr;};
void fake_vkGetDeviceQueue(VkDevice,unsigned,unsigned,VkQueue* queue){*queue=1;}
VkResult fake_vkQueueSubmit(VkQueue queue,unsigned,const VkSubmitInfo*,VkFence){assert(queue==1);return VK_SUCCESS;}
#define D(name) fake_##name
bool roomViewMode=false,roomViewReady=false;
struct RenderViewStub{void expected(const QuestGpuPresent&){}void acquired(VkSemaphore){}}renderedViews;
struct Swapchain{
 bool offscreen=false;int socket=-1;uint64_t presented=0;unsigned next=0,queueFamily=0;
 std::mutex lock;std::condition_variable presentedCondition;
 quest::FramePool framePool;uint64_t presentIds[QUEST_GPU_BUFFERS]{};
 bool released[QUEST_GPU_BUFFERS]{};uint64_t acquireCommands[QUEST_GPU_BUFFERS]{};
 std::chrono::steady_clock::time_point acquired[QUEST_GPU_BUFFERS]{};
 void sampleInputPose(unsigned){}
 QuestGpuPresent inputPoses[QUEST_GPU_BUFFERS]{};
''' + events + r'''
};
template<class T>T* object(VkSwapchainKHR handle){return reinterpret_cast<T*>(handle);}
''' + acquire + wait + r'''
using namespace std::chrono_literals;
void waiting(Swapchain& swap){
 auto deadline=std::chrono::steady_clock::now()+2s;
 for(;;){
  {std::lock_guard<std::mutex> guard(swap.lock);if(swap.eventWaiting)return;}
  assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();
 }
}
int main(){
 int sockets[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,sockets));
 Swapchain swap;swap.socket=sockets[0];assert(swap.framePool.initialize(7));
 for(unsigned i=0;i<3;i++){assert(swap.framePool.acquire()==int(i));swap.framePool.submitted(i,i+1);swap.presentIds[i]=i+1;}
 auto handle=reinterpret_cast<VkSwapchainKHR>(&swap);unsigned index=99;
 assert(vkAcquireNextImageKHR(0,handle,0,0,0,&index)==VK_NOT_READY);
 assert(vkAcquireNextImageKHR(0,handle,1000000,0,0,&index)==VK_TIMEOUT);
 assert(vkWaitForPresentKHR(0,handle,1,0)==VK_TIMEOUT);
 auto present=std::async(std::launch::async,[&]{return vkWaitForPresentKHR(0,handle,1,UINT64_MAX);});
 waiting(swap);
 // Another submission/acquisition must be able to hold the swapchain lock
 // while the present waiter is blocked on the compositor socket.
 assert(swap.lock.try_lock());swap.lock.unlock();
 auto acquiring=std::async(std::launch::async,[&]{return vkAcquireNextImageKHR(0,handle,UINT64_MAX,0,0,&index);});
 QuestGpuRelease accepted{QUEST_GPU_ACCEPTED_MAGIC,0,1};
 assert(send(sockets[1],&accepted,sizeof accepted,0)==sizeof accepted);
 assert(present.wait_for(2s)==std::future_status::ready&&present.get()==VK_SUCCESS);
 // Acceptance is not an image release. The second waiter must keep waiting.
 assert(acquiring.wait_for(10ms)==std::future_status::timeout);
 QuestGpuRelease release{QUEST_GPU_MAGIC,0,1};
 assert(send(sockets[1],&release,sizeof release,0)==sizeof release);
 assert(acquiring.wait_for(2s)==std::future_status::ready&&acquiring.get()==VK_SUCCESS&&index==0);
 // The acquisition reader must also let other swapchain operations proceed.
 auto acquiringAgain=std::async(std::launch::async,[&]{return vkAcquireNextImageKHR(0,handle,UINT64_MAX,0,0,&index);});
 waiting(swap);assert(swap.lock.try_lock());swap.lock.unlock();
 // Surface loss wakes the sole reader and releases the mutex.
 close(sockets[1]);
 assert(acquiringAgain.wait_for(2s)==std::future_status::ready&&acquiringAgain.get()==VK_ERROR_SURFACE_LOST_KHR);
 close(sockets[0]);
}
''')
    subprocess.run(['c++', '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'app/src/main/cpp'), str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
    print('WSI waits: concurrent present/acquire, lock availability, acceptance, release, timeouts and surface loss passed')
