#include "../app/src/main/cpp/gpu_frames.h"
#include <cassert>
#include <iostream>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

int main(){
 quest::FrameLeases consumer;quest::FramePool producer;
 assert(!producer.initialize(0)&&!producer.initialize(8));
 assert(producer.initialize(consumer.reconnect()));
 assert(!consumer.publish(3,1)&&!consumer.publish(0,0));
 auto publish=[&](uint64_t frame){int slot=producer.acquire();assert(slot>=0);producer.submitted(slot,frame);assert(consumer.publish(slot,frame));return slot;};
 auto release=[&]{for(unsigned i=0;i<3;i++){
  if(consumer.acceptedMask()&(1u<<i))consumer.sentAccepted(i);
  if(consumer.releaseMask()&(1u<<i)){assert(producer.release(i,consumer.frame(i)));consumer.sent(i);}
 }};
 int a=publish(1);assert(consumer.adopt()==a);
 assert(consumer.acceptedMask()==(1u<<a)&&!consumer.releaseMask());
 int b=publish(2),c=publish(3);assert(producer.acquire()==-1);
 assert(!consumer.publish(a,4)); // Both eye reads still own a.
 assert(consumer.releaseMask()==(1u<<b)); // Only the unread image is dropped.
 release();assert(consumer.adopt()==c);release();
 int d=publish(4);assert(d!=c&&!producer.release(d,1));
 // Reconnecting preserves the displayed allocation until a new one replaces it.
 quest::FramePool reconnected;assert(reconnected.initialize(consumer.reconnect()));
 int next=reconnected.acquire();assert(next>=0&&next!=c);
 reconnected.submitted(next,1);assert(consumer.publish(next,1));
 assert(consumer.adopt()==next&&reconnected.release(c,3));
 assert(!reconnected.release(c,3)&&!reconnected.release(3,1));

 // Concurrent producer pressure must never change pixels held by either eye.
 quest::FrameLeases leases;quest::FramePool pool;assert(pool.initialize(leases.reconnect()));
 std::array<uint64_t,64> pixels[3]{};std::mutex lock;std::atomic<bool> done{false};
 auto drain=[&]{for(unsigned i=0;i<3;i++){
  if(leases.acceptedMask()&(1u<<i))leases.sentAccepted(i);
  if(leases.releaseMask()&(1u<<i)){assert(pool.release(i,leases.frame(i)));leases.sent(i);}
 }};
 std::thread writer([&]{for(uint64_t frame=1;frame<=100000;frame++){
  int slot;
  for(;;){{std::lock_guard<std::mutex> guard(lock);drain();slot=pool.acquire();}if(slot>=0)break;std::this_thread::yield();}
  pixels[slot].fill(frame);
  {std::lock_guard<std::mutex> guard(lock);pool.submitted(slot,frame);assert(leases.publish(slot,frame));}
 }done.store(true);});
 uint64_t last=0;
 auto draw=[&]{int slot;{std::lock_guard<std::mutex> guard(lock);slot=leases.adopt();}if(slot<0)return;
  uint64_t frame=pixels[slot][0];assert(frame>last);
  for(int eye=0;eye<2;eye++)for(int pass=0;pass<8;pass++)for(auto pixel:pixels[slot])assert(pixel==frame);
  {std::lock_guard<std::mutex> guard(lock);}last=frame;
 };
 while(!done.load()){draw();std::this_thread::yield();}writer.join();draw();assert(last==100000);
 std::cout<<"Shared GPU frames: held-image ownership, stale releases, latest-frame selection, reconnect and stereo pressure passed\n";
}
