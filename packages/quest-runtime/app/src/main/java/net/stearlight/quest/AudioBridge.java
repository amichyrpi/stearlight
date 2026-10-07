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
  Session(boolean microphone){this.microphone=microphone;}
  void start(){new Thread(this,microphone?"Quest microphone":"Quest speakers").start();}
  synchronized void cancel(){cancelled=true;disconnect();}
  private synchronized void disconnect(){
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
       int size=Math.max(7680,AudioTrack.getMinBufferSize(RATE,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT));
       track=new AudioTrack.Builder().setAudioAttributes(attributes())
        .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
        .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(size).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY).build();track.play();
      }
      Log.i(TAG,"Quest speakers connected: stereo 48000 Hz");
      DataInputStream in=new DataInputStream(connection.getInputStream());byte[] pcm=new byte[1920];
      while(!cancelled){in.readFully(pcm);int offset=0;while(offset<pcm.length&&!cancelled){int count=track.write(pcm,offset,pcm.length-offset,AudioTrack.WRITE_BLOCKING);if(count<=0)throw new IOException("Speaker write: "+count);offset+=count;bytes+=count;}}
     }
     lastError=null;
    }catch(Exception error){if(!cancelled&&!java.util.Objects.equals(lastError,error.getMessage())){lastError=error.getMessage();Log.w(TAG,(microphone?"Microphone":"Speakers")+" waiting for Linux audio: "+lastError);}}
    finally {synchronized(this){disconnect();if(recorder!=null){recorder.release();recorder=null;}if(track!=null){track.release();track=null;}}
     if(bytes>0)Log.i(TAG,(microphone?"Microphone":"Speakers")+" transferred "+bytes+" PCM bytes");}
    if(!cancelled)try{Thread.sleep(500);}catch(InterruptedException ignored){break;}
   }
  }
 }
}
