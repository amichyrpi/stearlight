#include "../app/src/main/cpp/frame_history.h"
#include <cassert>
#include <iostream>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

int main(){
 quest::FrameHistory history;
 assert(history.adopt()==-1&&!history.publish(-1));
 int first=history.beginWrite();assert(first==0&&history.beginWrite()==-1);
 assert(!history.publish(1)&&history.publish(first));
 int second=history.beginWrite();assert(second==1);
 assert(history.adopt()==first); // A concurrent copy never replaces held pixels.
 assert(history.publish(second));
 int third=history.beginWrite();assert(third==2);
 assert(history.publish(third)); // Supersede an image that was never adopted.
 assert(history.adopt()==third&&history.adopt()==-1);
 // An unsignaled eye-read fence keeps the old image held while newer
 // completed copies supersede one another. Both eyes retain stable pixels.
 int pending=history.beginWrite();assert(pending!=third&&history.publish(pending));
 assert(history.adopt(false)==-1);
 int latest=history.beginWrite();assert(latest!=third&&latest!=pending);
 assert(history.publish(latest)&&history.adopt(false)==-1);
 assert(history.adopt(true)==latest);
 int reused=history.beginWrite();assert(reused==first);
 assert(history.publish(reused)&&history.adopt()==reused);
 for(int i=0;i<10000;i++){
  int slot=history.beginWrite();assert(slot>=0&&slot!=reused);
  assert(history.publish(slot));
  if(i%3==0)reused=history.adopt();
 }
 // Match the copy/render threads: publication is serialized, while writes
 // and reads of different slots run concurrently outside the state lock.
 quest::FrameHistory concurrent;std::mutex lock;std::atomic<bool> done{false};
 std::array<unsigned,64> pixels[3]{};
 std::thread producer([&]{
  for(unsigned frame=1;frame<=100000;frame++){
   int slot;{std::lock_guard<std::mutex> guard(lock);slot=concurrent.beginWrite();}
   assert(slot>=0);pixels[slot].fill(frame);
   {std::lock_guard<std::mutex> guard(lock);assert(concurrent.publish(slot));}
   if(frame%8==0)std::this_thread::yield();
  }done.store(true);
 });
 unsigned last=0;
 auto consume=[&]{
  int slot;{std::lock_guard<std::mutex> guard(lock);slot=concurrent.adopt();}
  if(slot<0)return;
  unsigned value=pixels[slot][0];assert(value>last);
  for(int repeat=0;repeat<8;repeat++)for(unsigned pixel:pixels[slot])assert(pixel==value);
  last=value;
 };
 while(!done.load()){consume();std::this_thread::yield();}
 producer.join();consume();assert(last==100000);
 std::cout<<"History ownership, pending-frame replacement and reuse passed\n";
}
