package net.stearlight.quest;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.concurrent.TimeUnit;

/** Run the bundled musl audio server without putting its deadlines in PRoot. */
final class NativeAudioServer implements AutoCloseable {
 private Process server;
 private File pidFile;
 void start(File root,File tmp,File state,File logs,File loader)throws IOException,InterruptedException {
  close();
  File runtime=new File(tmp,"stearlight-runtime/pulse");runtime.mkdirs();
  File saved=new File(state,"pulse-state");saved.mkdirs();
  File config=new File(state,"native-audio.pa");
  String text=new String(Files.readAllBytes(new File(root,"opt/quest/audio.pa").toPath()),StandardCharsets.UTF_8)
   .replace("load-module module-native-protocol-unix","load-module module-native-protocol-unix socket="+new File(runtime,"native"))
   .replace("/run/stearlight/quest",state.toString());
  Files.write(config.toPath(),text.getBytes(StandardCharsets.UTF_8));
  File modules=new File(root,"usr/lib/pulseaudio/modules");
  String libraries=new File(root,"lib")+":"+new File(root,"usr/lib")+":"+new File(root,"usr/lib/pulseaudio")+":"+modules;
  pidFile=new File(state,"native-audio.pid");Files.deleteIfExists(pidFile.toPath());
  ProcessBuilder builder=new ProcessBuilder("/system/bin/sh","-c","printf '%s\\n' \"$$\" > \"$1\" || exit; shift; export LD_PRELOAD=\"$1\"; shift; exec \"$@\"","stearlight-audio",pidFile.toString(),new File(root,"opt/quest/libquest_audio_identity.so").toString(),
   loader.toString(),"--library-path",libraries,new File(root,"usr/bin/pulseaudio").toString(),
   "--daemonize=no","--exit-idle-time=-1","--use-pid-file=no","--disable-shm=yes","-n","--file="+config);
  Map<String,String> env=builder.environment();env.remove("LD_PRELOAD");env.remove("LD_LIBRARY_PATH");
  env.put("HOME",new File(root,"home/stearlight").toString());env.put("TMPDIR",tmp.toString());
  env.put("PULSE_RUNTIME_PATH",runtime.toString());env.put("PULSE_STATE_PATH",saved.toString());env.put("PULSE_DLPATH",modules.toString());
  builder.redirectErrorStream(true);builder.redirectOutput(new File(logs,"audio-server.log"));
  server=builder.start();
  File socket=new File(runtime,"native");
  for(int attempt=0;attempt<100;attempt++){
   if(!server.isAlive())throw new IOException("Audio server stopped; see audio-server.log");
   if(socket.exists())return;
   Thread.sleep(20);
  }
  throw new IOException("Audio server startup timed out; see audio-server.log");
 }
 @Override public void close(){
  if(server==null)return;
  boolean interrupted=Thread.interrupted();
  try{
   server.destroy();
   try{server.waitFor(1,TimeUnit.SECONDS);}catch(InterruptedException e){interrupted=true;}
   try{if(server.isAlive()&&pidFile!=null){
    int pid=Integer.parseInt(new String(Files.readAllBytes(pidFile.toPath()),StandardCharsets.US_ASCII).trim());
    if(pid>1&&pid!=android.os.Process.myPid())android.os.Process.killProcess(pid);
   }}catch(IOException|NumberFormatException ignored){}
  }finally{server=null;if(interrupted)Thread.currentThread().interrupt();}
 }
}
