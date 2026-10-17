package net.stearlight.quest;

import android.util.Log;
import android.content.Intent;
import android.provider.Settings;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/** Launch optional desktop runtimes beside the OS's existing PRoot tracer. */
final class DesktopAppBridge implements AutoCloseable {
 private final QuestActivity activity;
 private final File files, rootfs, requests;
 private final Map<String,Process> processes=new HashMap<>();
 private final Set<String> allowedEnvironment=new HashSet<>(Arrays.asList(
  "HOME","USER","LOGNAME","PATH","LANG","LC_ALL","TERM","DISPLAY","WAYLAND_DISPLAY",
  "XDG_RUNTIME_DIR","XDG_CONFIG_HOME","XDG_DATA_HOME","XDG_CACHE_HOME","XDG_DATA_DIRS",
  "XDG_CURRENT_DESKTOP","XDG_SESSION_TYPE","DBUS_SESSION_BUS_ADDRESS","PULSE_SERVER",
  "QT_QPA_PLATFORM","QT_QUICK_BACKEND","QT_XCB_GL_INTEGRATION","QT_XCB_NO_MITSHM",
  "GDK_BACKEND","GDK_DEBUG","GSK_RENDERER","GTK_USE_PORTAL","GIO_USE_VFS",
  "LIBGL_ALWAYS_SOFTWARE","FLATPAK_ID","TMPDIR","GSETTINGS_SCHEMA_DIR","FONTCONFIG_FILE","LD_LIBRARY_PATH"));
 DesktopAppBridge(QuestActivity owner,File directory){
  activity=owner;files=owner.getFilesDir();rootfs=new File(files,"rootfs");
  requests=new File(directory,"desktop-app-requests");requests.mkdirs();
  new File(directory,"desktop-device-settings.request").delete();
 }
 private File guest(String path)throws IOException {
  if(!path.startsWith("/")||path.indexOf(':')>=0||path.indexOf('\0')>=0)throw new IOException("Invalid guest path");
  File result=new File(rootfs,path.substring(1)).getCanonicalFile();
  if(!result.getPath().startsWith(rootfs.getCanonicalPath()+File.separator))throw new IOException("Path outside Linux files");
  if(!result.exists())throw new IOException("Desktop runtime file is missing");
  return result;
 }
 private void bind(List<String> args,File source,String target)throws IOException {
  if(!target.startsWith("/")||target.contains("..")||target.indexOf(':')>=0||target.indexOf('\0')>=0)throw new IOException("Invalid bind target");
  args.add("-b");args.add(source.getCanonicalPath()+":"+target);
 }
 private void result(String id,String state,int code,String error){
  try{
   JSONObject value=new JSONObject().put("state",state).put("code",code).put("error",error);
   File target=new File(requests,id+".result"),temporary=new File(requests,id+".next");
   try(FileOutputStream out=new FileOutputStream(temporary)){out.write(value.toString().getBytes(StandardCharsets.UTF_8));}
   if(!temporary.renameTo(target))throw new IOException("Desktop result rename failed");
  }catch(Exception errorWriting){Log.w("StearlightDesktop","Cannot write app state",errorWriting);}
 }
 private void start(String id,JSONObject request)throws Exception {
  if(processes.size()>=4)throw new IOException("Close another desktop application first");
  File root=guest(request.getString("root"));
  String privateHome=new File(rootfs,"home/stearlight/.local/share/stearlight/").getCanonicalPath()+File.separator;
  if(!root.isDirectory()||!root.getPath().startsWith(privateHome))throw new IOException("Invalid desktop runtime root");
  String nativeDir=activity.getApplicationInfo().nativeLibraryDir;
  List<String> args=new ArrayList<>(Arrays.asList(nativeDir+"/libproot_exec.so","--kill-on-exit","--link2symlink","-r",root.getCanonicalPath()));
  for(String path:new String[]{"/dev","/proc","/sys"}){args.add("-b");args.add(path);}
  // PRoot's link2symlink stores OSTree hardlinks as absolute Android paths.
  // Keep those backing objects reachable from the separate application root.
  bind(args,rootfs,rootfs.getAbsolutePath());
  if(!rootfs.getCanonicalPath().equals(rootfs.getAbsolutePath()))bind(args,rootfs,rootfs.getCanonicalPath());
  bind(args,new File(rootfs,"home/stearlight"),"/home/stearlight");
  bind(args,new File(files,"tmp"),"/tmp");bind(args,new File(files,"shm"),"/dev/shm");
  bind(args,new File(activity.getStateDirectory()),"/run/stearlight/quest");
  JSONArray bindings=request.optJSONArray("bindings");
  if(bindings!=null){if(bindings.length()>32)throw new IOException("Too many app bindings");for(int i=0;i<bindings.length();i++){
   JSONObject entry=bindings.getJSONObject(i);bind(args,guest(entry.getString("source")),entry.getString("target"));
  }}
  String cwd=request.optString("cwd","/home/stearlight");
  if(!cwd.startsWith("/")||cwd.indexOf('\0')>=0)throw new IOException("Invalid working directory");
  args.add("-w");args.add(cwd);args.add("/usr/bin/env");args.add("-i");
  JSONObject environment=request.getJSONObject("environment");
  for(Iterator<String> keys=environment.keys();keys.hasNext();){String key=keys.next(),value=environment.getString(key);
   if(!allowedEnvironment.contains(key)||value.length()>8192||value.indexOf('\0')>=0)throw new IOException("Invalid app environment");
   args.add(key+"="+value);
  }
  JSONArray command=request.getJSONArray("command");if(command.length()==0||command.length()>128)throw new IOException("Invalid app command");
  for(int i=0;i<command.length();i++){String arg=command.getString(i);if(arg.length()>8192||arg.indexOf('\0')>=0)throw new IOException("Invalid app argument");args.add(arg);}
  ProcessBuilder builder=new ProcessBuilder(args);Map<String,String> env=builder.environment();
  env.put("LD_LIBRARY_PATH",nativeDir);env.remove("LD_PRELOAD");env.put("PROOT_LOADER",nativeDir+"/libproot_loader.so");
  env.put("PROOT_TMP_DIR",new File(files,"tmp").getPath());env.put("PROOT_QUEST_ROBUST","1");env.remove("PROOT_NO_SECCOMP");
  File logs=new File(activity.getStateDirectory(),"desktop-app-logs");logs.mkdirs();builder.redirectErrorStream(true);builder.redirectOutput(new File(logs,"desktop-app-"+id+".log"));
  Process process=builder.start();processes.put(id,process);result(id,"running",0,"");
 }
 synchronized void poll(){
  if(!new File(activity.getStateDirectory(),"desktop-mode.active").isFile()){close();return;}
  File settings=new File(activity.getStateDirectory(),"desktop-device-settings.request");
  if(settings.isFile()){
   try{
    String name=settings.length()<=32?new String(Files.readAllBytes(settings.toPath()),StandardCharsets.UTF_8).trim():"";
    settings.delete();
    String action=name.equals("wifi")?Settings.ACTION_WIFI_SETTINGS:name.equals("bluetooth")?Settings.ACTION_BLUETOOTH_SETTINGS:name.equals("headset")?Settings.ACTION_SETTINGS:null;
    if(action!=null)activity.runOnUiThread(()->{try{activity.startActivity(new Intent(action));}catch(Exception error){Log.w("StearlightDesktop","Headset settings unavailable",error);}});
   }catch(IOException error){settings.delete();Log.w("StearlightDesktop","Cannot read settings request",error);}
  }
  for(Iterator<Map.Entry<String,Process>> it=processes.entrySet().iterator();it.hasNext();){
   Map.Entry<String,Process> entry=it.next();if(!entry.getValue().isAlive()){result(entry.getKey(),"exited",entry.getValue().exitValue(),"");it.remove();}
  }
  File[] pending=requests.listFiles((directory,name)->name.matches("[a-f0-9]{32}\\.request"));if(pending==null)return;
  for(File file:pending){String id=file.getName().substring(0,32);
   try{
    if(!file.getCanonicalFile().getParentFile().equals(requests.getCanonicalFile())||file.length()>65536)throw new IOException("Invalid app request");
    JSONObject request=new JSONObject(new String(Files.readAllBytes(file.toPath()),StandardCharsets.UTF_8));
    if(!file.delete())continue;start(id,request);
   }catch(Exception error){file.delete();result(id,"failed",127,error.getMessage());Log.w("StearlightDesktop","Desktop app launch failed",error);}
  }
 }
 @Override public synchronized void close(){
  for(Map.Entry<String,Process> entry:processes.entrySet()){entry.getValue().destroy();result(entry.getKey(),"exited",143,"");}processes.clear();
  File[] pending=requests.listFiles((directory,name)->name.matches("[a-f0-9]{32}\\.request"));
  if(pending!=null)for(File file:pending){if(file.delete())result(file.getName().substring(0,32),"failed",143,"Open Desktop before launching the application");}
 }
}
