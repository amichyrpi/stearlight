package net.stearlight.quest;

import java.io.IOException;

/** A bounded, frame-aligned queue. A delayed consumer resumes at recent audio. */
final class PcmQueue {
 private final byte[] data;
 private final int frameBytes;
 private int head,size;
 private long dropped;
 private IOException failure;
 private final int fadeFrames;
 private int fadeRemaining,fadeLeft,fadeRight,lastLeft,lastRight;
 private boolean resync,played;

 PcmQueue(int capacity,int frameBytes){this(capacity,frameBytes,0);}
 PcmQueue(int capacity,int frameBytes,int fadeFrames){
  if(frameBytes<=0||capacity<=0||capacity%frameBytes!=0)throw new IllegalArgumentException("PCM capacity");
  if(fadeFrames<0||(fadeFrames>0&&frameBytes!=4))throw new IllegalArgumentException("PCM fade");
  data=new byte[capacity];this.frameBytes=frameBytes;
  this.fadeFrames=fadeFrames;
 }
 synchronized void put(byte[] source,int length)throws IOException {
  check();
  if(length<0||length>source.length||length%frameBytes!=0)throw new IllegalArgumentException("PCM frames");
  int offset=Math.max(0,length-data.length);
  int discard=Math.max(0,size+length-offset-data.length);
  head=(head+discard)%data.length;size-=discard;dropped+=discard+offset;
  if(discard+offset>0)resync=true;
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
  if(fadeFrames>0){
   // Only a forced backlog skip changes the waveform. Blend its boundary
   // over 5 ms instead of jumping directly between unrelated samples.
   if(resync){if(played){fadeRemaining=fadeFrames;fadeLeft=lastLeft;fadeRight=lastRight;}resync=false;}
   for(int i=0;i<target.length&&fadeRemaining>0;i+=4){
    int progress=fadeFrames-fadeRemaining+1;
    sample(target,i,fadeLeft+(sample(target,i)-fadeLeft)*progress/fadeFrames);
    sample(target,i+2,fadeRight+(sample(target,i+2)-fadeRight)*progress/fadeFrames);
    fadeRemaining--;
   }
   lastLeft=sample(target,target.length-4);lastRight=sample(target,target.length-2);played=true;
  }
 }
 synchronized void close(IOException error){if(failure==null)failure=error;notifyAll();}
 synchronized int queuedFrames(){return size/frameBytes;}
 synchronized long droppedFrames(){return dropped/frameBytes;}
 private void check()throws IOException {if(failure!=null)throw failure;}
 private static int sample(byte[] data,int offset){return (short)((data[offset]&255)|(data[offset+1]<<8));}
 private static void sample(byte[] data,int offset,int value){data[offset]=(byte)value;data[offset+1]=(byte)(value>>8);}
}
