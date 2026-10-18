package net.stearlight.quest;

import java.io.IOException;

/** A bounded, frame-aligned queue. A delayed consumer resumes at recent audio. */
final class PcmQueue {
 private final byte[] data;
 private final int frameBytes;
 private int head,size;
 private long dropped;
 private IOException failure;

 PcmQueue(int capacity,int frameBytes){
  if(frameBytes<=0||capacity<=0||capacity%frameBytes!=0)throw new IllegalArgumentException("PCM capacity");
  data=new byte[capacity];this.frameBytes=frameBytes;
 }
 synchronized void put(byte[] source,int length)throws IOException {
  check();
  if(length<0||length>source.length||length%frameBytes!=0)throw new IllegalArgumentException("PCM frames");
  int offset=Math.max(0,length-data.length);
  int discard=Math.max(0,size+length-offset-data.length);
  head=(head+discard)%data.length;size-=discard;dropped+=discard+offset;
  int tail=(head+size)%data.length,count=length-offset;
  int first=Math.min(count,data.length-tail);
  System.arraycopy(source,offset,data,tail,first);
  System.arraycopy(source,offset+first,data,0,count-first);
  size+=count;notifyAll();
 }
 synchronized void take(byte[] target)throws IOException,InterruptedException {
  if(target.length==0||target.length>data.length||target.length%frameBytes!=0)throw new IllegalArgumentException("PCM frames");
  while(size<target.length){check();wait();}
  check();
  int first=Math.min(target.length,data.length-head);
  System.arraycopy(data,head,target,0,first);
  System.arraycopy(data,0,target,first,target.length-first);
  head=(head+target.length)%data.length;size-=target.length;
 }
 synchronized void close(IOException error){if(failure==null)failure=error;notifyAll();}
 synchronized int queuedFrames(){return size/frameBytes;}
 synchronized long droppedFrames(){return dropped/frameBytes;}
 private void check()throws IOException {if(failure!=null)throw failure;}
}
