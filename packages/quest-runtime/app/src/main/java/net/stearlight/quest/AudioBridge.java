package net.stearlight.quest;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.*;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.*;

/** Fixed-format PCM transport; all blocking I/O stays off the OpenXR thread. */
final class AudioBridge implements AutoCloseable {
 private static final String TAG="StearlightAudio";
 private static final int RATE=48000;
 private static final int FRAME_BYTES=4,CHUNK_FRAMES=240; // 5 ms stereo PCM.
 private final Context context;
 private final File state;
 private final AudioManager manager;
 private final AudioFocusRequest focus;
 private final Handler main=new Handler(Looper.getMainLooper());
 private boolean resumed,xrFocused,hasFocus,closed;
 private Session output,input;

 AudioBridge(Context context,File state){
  this.context=context;this.state=state;
  manager=(AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
  focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
   .setAudioAttributes(attributes()).setOnAudioFocusChangeListener(change->{
    hasFocus=change==AudioManager.AUDIOFOCUS_GAIN;updateStreams();
   },main).build();
 }
 private static AudioAttributes attributes(){return new AudioAttributes.Builder()
  .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();}
 void setResumed(boolean value){resumed=value;updateFocus();}
 void setXrFocused(boolean value){xrFocused=value;updateFocus();}
 void permissionsChanged(){updateStreams();}
 private void updateFocus(){
  if(closed)return;
  if(resumed&&xrFocused){if(!hasFocus)hasFocus=manager.requestAudioFocus(focus)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;}
  else {hasFocus=false;manager.abandonAudioFocusRequest(focus);}
  updateStreams();
 }
 private void updateStreams(){
  boolean active=!closed&&resumed&&xrFocused&&hasFocus;
  if(active&&output==null){output=new Session(false);output.start();}
  if(!active&&output!=null){output.cancel();output=null;}
  boolean mic=active&&context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED;
  if(mic&&input==null){input=new Session(true);input.start();}
  if(!mic&&input!=null){input.cancel();input=null;}
 }
 @Override public void close(){closed=true;hasFocus=false;manager.abandonAudioFocusRequest(focus);updateStreams();}

 private final class Session implements Runnable {
  final boolean microphone;
  volatile boolean cancelled;
  LocalSocket socket;
  AudioTrack track;
  AudioRecord recorder;
  PcmQueue playback;
  volatile long maxReadGapNs;
  Session(boolean microphone){this.microphone=microphone;}
  void start(){new Thread(this,microphone?"Quest microphone":"Quest speakers").start();}
  synchronized void cancel(){cancelled=true;disconnect();}
  private synchronized void disconnect(){
   if(playback!=null)playback.close(new IOException("Audio stopped"));
   if(socket!=null){try{socket.shutdownInput();socket.shutdownOutput();}catch(IOException ignored){}try{socket.close();}catch(IOException ignored){}socket=null;}
   if(recorder!=null){try{recorder.stop();}catch(IllegalStateException ignored){}}
   if(track!=null){try{track.pause();track.flush();}catch(IllegalStateException ignored){}}
  }
  private synchronized LocalSocket connect()throws IOException {
   if(cancelled)throw new IOException("Audio stopped");
   socket=new LocalSocket();
   socket.connect(new LocalSocketAddress(new File(state,microphone?"audio-input.sock":"audio-output.sock").toString(),LocalSocketAddress.Namespace.FILESYSTEM));
   socket.setSoTimeout(2000);socket.setReceiveBufferSize(8192);socket.setSendBufferSize(8192);
   return socket;
  }
  @Override public void run(){
   android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);
   String lastError=null;
   while(!cancelled){
    long bytes=0;
    Thread reader=null;
    try {
     LocalSocket connection=connect();
     if(microphone){
      synchronized(this){
       if(cancelled)break;
       int size=Math.max(3840,AudioRecord.getMinBufferSize(RATE,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT));
       recorder=new AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.VOICE_RECOGNITION)
        .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_IN_MONO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
        .setBufferSizeInBytes(size).build();recorder.startRecording();
      }
      Log.i(TAG,"Quest microphone connected: mono 48000 Hz");
      OutputStream out=connection.getOutputStream();byte[] pcm=new byte[960];
      while(!cancelled){int count=recorder.read(pcm,0,pcm.length,AudioRecord.READ_BLOCKING);if(count<=0)throw new IOException("Microphone read: "+count);out.write(pcm,0,count);bytes+=count;}
     }else{
      synchronized(this){
       if(cancelled)break;
       int minimum=AudioTrack.getMinBufferSize(RATE,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT);
       if(minimum<=0)throw new IOException("Speaker buffer: "+minimum);
       int frames=Math.max(RATE/25,(minimum+FRAME_BYTES-1)/FRAME_BYTES);
       int capacity=Math.max(frames,RATE*80/1000);
       track=new AudioTrack.Builder().setAudioAttributes(attributes())
        .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
        .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(capacity*FRAME_BYTES).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY).build();
       if(track.getState()!=AudioTrack.STATE_INITIALIZED)throw new IOException("Speaker initialization failed");
       if(track.setBufferSizeInFrames(frames)<0)throw new IOException("Speaker buffer configuration failed");
       if(android.os.Build.VERSION.SDK_INT>=31)track.setStartThresholdInFrames(track.getBufferSizeInFrames());
       playback=new PcmQueue(RATE*60/1000*FRAME_BYTES,FRAME_BYTES);
      }
      final PcmQueue queue=playback;
      maxReadGapNs=0;
      // Always drain Pulse independently of AudioTrack's blocking writes. The
      // simple protocol otherwise retains seconds of audio after a CPU stall.
      reader=new Thread(()->readSpeakers(connection,queue),"Quest PCM receiver");reader.start();
      Log.i(TAG,"Quest speakers connected: stereo 48000 Hz, buffer="+track.getBufferSizeInFrames()+" frames");
      byte[] pcm=new byte[CHUNK_FRAMES*FRAME_BYTES];
      int underruns=track.getUnderrunCount();boolean playing=false;
      long report=System.nanoTime();
      while(!cancelled){
       queue.take(pcm);int offset=0;
       while(offset<pcm.length&&!cancelled){
        // Start only after priming. Never fill a paused track beyond its
        // effective size, including devices whose frame count isn't 240-aligned.
        int length=pcm.length-offset;
        if(!playing)length=(int)Math.min(length,track.getBufferSizeInFrames()*FRAME_BYTES-bytes);
        int count=track.write(pcm,offset,length,AudioTrack.WRITE_BLOCKING);
        if(count<=0)throw new IOException("Speaker write: "+count);
        offset+=count;bytes+=count;
        if(!playing&&bytes>=track.getBufferSizeInFrames()*FRAME_BYTES){track.play();playing=true;}
       }
       int current=track.getUnderrunCount();
       if(current>underruns){
        // Add one 5 ms burst of scheduling headroom, with an 80 ms ceiling.
        int frames=Math.min(track.getBufferCapacityInFrames(),track.getBufferSizeInFrames()+CHUNK_FRAMES);
        track.setBufferSizeInFrames(frames);underruns=current;
       }
       long now=System.nanoTime();
       if(now-report>=30_000_000_000L){
        Log.i(TAG,"Playback: underruns="+current+" buffer_frames="+track.getBufferSizeInFrames()+" queued_frames="+queue.queuedFrames()+" dropped_frames="+queue.droppedFrames()+" max_read_gap_ms="+maxReadGapNs/1_000_000);
        report=now;maxReadGapNs=0;
       }
      }
     }
     lastError=null;
    }catch(Exception error){if(!cancelled&&!java.util.Objects.equals(lastError,error.getMessage())){lastError=error.getMessage();Log.w(TAG,(microphone?"Microphone":"Speakers")+" waiting for Linux audio: "+lastError);}}
    finally {synchronized(this){disconnect();if(recorder!=null){recorder.release();recorder=null;}if(track!=null){track.release();track=null;}playback=null;}
     if(reader!=null)try{reader.join(1000);}catch(InterruptedException ignored){Thread.currentThread().interrupt();}
     if(bytes>0)Log.i(TAG,(microphone?"Microphone":"Speakers")+" transferred "+bytes+" PCM bytes");}
    if(!cancelled)try{Thread.sleep(500);}catch(InterruptedException ignored){break;}
   }
  }
  private void readSpeakers(LocalSocket connection,PcmQueue queue){
   android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);
   try{
    DataInputStream in=new DataInputStream(connection.getInputStream());
    byte[] pcm=new byte[CHUNK_FRAMES*FRAME_BYTES];long previous=System.nanoTime();
    while(!cancelled){
     in.readFully(pcm);long now=System.nanoTime();
     maxReadGapNs=Math.max(maxReadGapNs,now-previous);previous=now;
     queue.put(pcm,pcm.length);
    }
   }catch(IOException error){queue.close(error);}
   finally{queue.close(new IOException("Speaker source closed"));}
  }
 }
}
