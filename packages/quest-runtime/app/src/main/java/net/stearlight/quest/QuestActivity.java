package net.stearlight.quest;

import android.Manifest;
import android.app.NativeActivity;
import android.bluetooth.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.net.*;
import android.os.*;
import android.util.Log;
import android.view.InputDevice;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;

public final class QuestActivity extends NativeActivity {
 static { System.loadLibrary("stearlight_quest"); }
 private final ScheduledExecutorService workers=Executors.newScheduledThreadPool(2);
 private volatile java.lang.Process linux;
 private volatile boolean closing;
 private final AtomicBoolean starting=new AtomicBoolean();
 private volatile int batteryPercent=-1, batteryStatus=BatteryManager.BATTERY_STATUS_UNKNOWN, batteryPlugged, batteryVoltage, batteryTemperature;
 private volatile boolean charging;
 private BluetoothBridge bluetoothBridge;
 private AudioBridge audioBridge;
 private NetworkBridge networkBridge;
 private UsbNetworkBridge usbNetworkBridge;
 private DesktopAppBridge desktopAppBridge;
 private ClockBridge clockBridge;
 native void setUsbNetwork(long handle);
 native int networkInterfaceIndex(String name);
 private final BroadcastReceiver batteryReceiver=new BroadcastReceiver(){public void onReceive(Context context,Intent intent){int level=intent.getIntExtra(BatteryManager.EXTRA_LEVEL,-1),scale=intent.getIntExtra(BatteryManager.EXTRA_SCALE,100);batteryPercent=level<0?-1:Math.round(level*100f/Math.max(1,scale));batteryStatus=intent.getIntExtra(BatteryManager.EXTRA_STATUS,BatteryManager.BATTERY_STATUS_UNKNOWN);charging=batteryStatus==BatteryManager.BATTERY_STATUS_CHARGING||batteryStatus==BatteryManager.BATTERY_STATUS_FULL;batteryPlugged=intent.getIntExtra(BatteryManager.EXTRA_PLUGGED,0);batteryVoltage=intent.getIntExtra(BatteryManager.EXTRA_VOLTAGE,0);batteryTemperature=intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE,0);}};
 @Override public void onCreate(Bundle state){
  new File(getStateDirectory(),"dashboard.ready").delete();
  new File(getStateDirectory(),"gpu-capture.request").delete();
  new File(getStateDirectory(),"xr-eye-capture.request").delete();
  super.onCreate(state);
  clockBridge=new ClockBridge(new File(getFilesDir(),"rootfs"));
  registerReceiver(batteryReceiver,new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
  audioBridge=new AudioBridge(this,new File(getStateDirectory()));
  List<String> missing=new ArrayList<>();for(String p:new String[]{Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_ADVERTISE,Manifest.permission.RECORD_AUDIO})if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED)missing.add(p);
  if(!missing.isEmpty())requestPermissions(missing.toArray(new String[0]),7);
  workers.scheduleWithFixedDelay(this::writeDeviceState,0,1,TimeUnit.SECONDS);
  networkBridge=new NetworkBridge(this,new File(getStateDirectory()));
  usbNetworkBridge=new UsbNetworkBridge(this,new File(getStateDirectory()));
  workers.scheduleWithFixedDelay(networkBridge::poll,0,1,TimeUnit.SECONDS);
  bluetoothBridge=new BluetoothBridge(this,new File(getStateDirectory()));
  workers.scheduleWithFixedDelay(bluetoothBridge::poll,0,25,TimeUnit.MILLISECONDS);
  desktopAppBridge=new DesktopAppBridge(this,new File(getStateDirectory()));
  workers.scheduleWithFixedDelay(desktopAppBridge::poll,0,100,TimeUnit.MILLISECONDS);
  requestLinux();
 }
 @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);requestLinux();}
 private void requestLinux(){if(!closing&&(linux==null||!linux.isAlive())&&starting.compareAndSet(false,true))workers.execute(()->{try{startLinux();}finally{starting.set(false);}});}
 public String getStateDirectory(){File state=new File(getFilesDir(),"state");state.mkdirs();return state.toString();}
 @Override public void onResume(){super.onResume();if(audioBridge!=null)audioBridge.setResumed(true);if(usbNetworkBridge!=null)usbNetworkBridge.resumed(true);requestLinux();}
 @Override public void onPause(){if(audioBridge!=null)audioBridge.setResumed(false);if(usbNetworkBridge!=null)usbNetworkBridge.resumed(false);super.onPause();}
 public void onXrAudioFocus(boolean focused){runOnUiThread(()->{if(!closing&&audioBridge!=null)audioBridge.setXrFocused(focused);});}
 @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){super.onRequestPermissionsResult(request,permissions,results);if(audioBridge!=null)audioBridge.permissionsChanged();}
 private BootAnimation bootAnimation;
 private native void startSocketRegistry(String directory);
 private native void stopSocketRegistry();
 public android.graphics.Bitmap getBootBitmap(){if(bootAnimation==null)bootAnimation=new BootAnimation(getAssets());return bootAnimation.frame();}
 public boolean bootComplete(){if(bootAnimation==null)bootAnimation=new BootAnimation(getAssets());return bootAnimation.bootComplete();}
 private void writeDeviceState(){
  if(closing)return;
  syncClock();
  try {
   JSONObject state=new JSONObject();state.put("battery_percent",batteryPercent);state.put("charging",charging);
   state.put("battery_status",batteryStatus);state.put("battery_plugged",batteryPlugged);state.put("battery_voltage_mv",batteryVoltage);state.put("battery_temperature_tenths_c",batteryTemperature);
   BatteryManager power=(BatteryManager)getSystemService(BATTERY_SERVICE);state.put("battery_charge_time_ms",power==null?-1:power.computeChargeTimeRemaining());
   ConnectivityManager cm=(ConnectivityManager)getSystemService(CONNECTIVITY_SERVICE);Network network=cm.getActiveNetwork();NetworkCapabilities caps=cm.getNetworkCapabilities(network);LinkProperties link=cm.getLinkProperties(network);
   state.put("connected",caps!=null&&caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET));state.put("validated",caps!=null&&caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED));state.put("wifi",caps!=null&&caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI));
   JSONArray dns=new JSONArray(),addresses=new JSONArray();if(link!=null){for(java.net.InetAddress address:link.getDnsServers())dns.put(address.getHostAddress());for(LinkAddress address:link.getLinkAddresses())addresses.put(address.toString());}state.put("dns",dns);state.put("addresses",addresses);
   boolean btPermission=checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;BluetoothManager manager=(BluetoothManager)getSystemService(BLUETOOTH_SERVICE);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();state.put("bluetooth_available",adapter!=null);state.put("bluetooth_permission",btPermission);state.put("bluetooth_enabled",btPermission&&adapter!=null&&adapter.isEnabled());state.put("bluetooth_le_advertising",btPermission&&adapter!=null&&adapter.isMultipleAdvertisementSupported());
   JSONArray input=new JSONArray();for(int id:InputDevice.getDeviceIds()){InputDevice device=InputDevice.getDevice(id);if(device==null)continue;android.hardware.BatteryState battery=device.getBatteryState();JSONObject item=new JSONObject();item.put("name",device.getName());item.put("sources",device.getSources());item.put("battery_present",battery.isPresent());item.put("battery_capacity",battery.isPresent()?battery.getCapacity():-1);input.put(item);}state.put("input_device_batteries",input);
   File directory=new File(getStateDirectory()),temporary=new File(directory,"device.json.tmp"),target=new File(directory,"device.json");try(FileOutputStream out=new FileOutputStream(temporary)){out.write(state.toString().getBytes(StandardCharsets.UTF_8));}if(!temporary.renameTo(target))throw new IOException("Cannot publish device state");
   if(link!=null){File root=new File(getFilesDir(),"rootfs"),resolv=new File(root,"etc/resolv.conf");if(resolv.exists()){StringBuilder text=new StringBuilder();for(java.net.InetAddress address:link.getDnsServers())text.append("nameserver ").append(address.getHostAddress()).append('\n');try(FileOutputStream out=new FileOutputStream(resolv)){out.write(text.toString().getBytes(StandardCharsets.US_ASCII));}}}
  }catch(Exception e){Log.w("StearlightQuest","Device state update failed",e);}
 }
 private void syncClock(){try{clockBridge.sync(TimeZone.getDefault().getID());}catch(IOException e){Log.w("StearlightQuest","Timezone sync failed",e);}}
 private void status(String value) throws IOException {File dir=new File(getStateDirectory());try(FileOutputStream out=new FileOutputStream(new File(dir,"linux-status"))){out.write(value.getBytes(StandardCharsets.UTF_8));}Log.i("StearlightQuest",value);}
 private void startLinux(){
  // Quest can recreate the Activity in the same Android process. Keep the
  // old session's shutdown ahead of the next session's temporary-file cleanup.
  try(RandomAccessFile file=new RandomAccessFile(new File(getFilesDir(),"linux-session.lock"),"rw");FileChannel channel=file.getChannel()){
   FileLock lock=null;
   while(!closing&&lock==null){try{lock=channel.tryLock();}catch(OverlappingFileLockException busy){}if(lock==null)Thread.sleep(50);}
   if(lock==null)return;
   try{runLinux();}finally{stopLinux();stopSocketRegistry();lock.release();}
  }catch(Exception error){if(!closing)Log.e("StearlightQuest","Linux session failed",error);}
 }
 private void stopLinux(){
  java.lang.Process process=linux;if(process==null)return;
  boolean interrupted=Thread.interrupted();killLinux();
  try{while(process.isAlive()){try{process.waitFor(100,TimeUnit.MILLISECONDS);}catch(InterruptedException stop){interrupted=true;}}}
  finally{if(!process.isAlive())linux=null;if(interrupted)Thread.currentThread().interrupt();}
 }
 private void killLinux(){
  java.lang.Process process=linux;if(process==null||!process.isAlive())return;
  // Android's Process.destroyForcibly() inherits destroy(), which sends TERM.
  // PRoot can inherit ART's blocked signals; use KILL for its owned session.
  try{
   File pidFile=new File(getFilesDir(),"linux.pid");String value="";
   for(int attempt=0;attempt<100&&process.isAlive();attempt++){
    if(pidFile.exists())value=new String(java.nio.file.Files.readAllBytes(pidFile.toPath()),StandardCharsets.US_ASCII).trim();
    if(value.matches("[0-9]+"))break;SystemClock.sleep(10);
   }
   if(!process.isAlive())return;int pid=Integer.parseInt(value);
   if(pid<=1||pid==android.os.Process.myPid())throw new IOException("Invalid Linux pid");
   if(process.isAlive())android.os.Process.killProcess(pid);
  }
  catch(Exception error){Log.e("StearlightQuest","Cannot kill Linux session",error);process.destroyForcibly();}
 }
 private void runLinux(){
  try {
   if(closing)return;
   new File(getStateDirectory(),"external-runtime.ready").delete();
   File root=new File(getFilesDir(),"rootfs"),marker=new File(root,".stearlight-ready");
   if(!marker.exists()){
    File staging=new File(getFilesDir(),"rootfs-install");removeIncomplete(staging);
    status("Installing Stearlight OS");
    MessageDigest digest=MessageDigest.getInstance("SHA-256");
    try(InputStream in=new DigestInputStream(rootfsStream(),digest)){RootfsArchive.extract(in,staging);}
    StringBuilder actual=new StringBuilder();for(byte b:digest.digest())actual.append(String.format(Locale.ROOT,"%02x",b&255));
    String expected;try(BufferedReader in=new BufferedReader(new InputStreamReader(getAssets().open("rootfs.sha256"),StandardCharsets.US_ASCII))){expected=in.readLine();}
    if(expected==null||!expected.matches("[0-9a-f]{64}")||!expected.equals(actual.toString()))throw new IOException("Rootfs payload checksum mismatch");
    if(root.exists())throw new IOException("Existing rootfs has no installation marker; preserving it for recovery");
    if(!staging.renameTo(root))throw new IOException("Cannot complete rootfs installation");
    try(FileOutputStream out=new FileOutputStream(marker)){out.write("1\n".getBytes(StandardCharsets.US_ASCII));}
   }
   String overlayVersion=Long.toString(getPackageManager().getPackageInfo(getPackageName(),0).lastUpdateTime);
   File overlayMarker=new File(getFilesDir(),"quest-overlay-version");
   String installed=overlayMarker.exists()?new String(java.nio.file.Files.readAllBytes(overlayMarker.toPath()),StandardCharsets.US_ASCII):"";
   if(!installed.equals(overlayVersion)){
    status("Preparing Stearlight runtime");
    boolean preserveRuntime=new File(root,"home/stearlight/.cache/stearlight/os-update/installed.json").isFile()
       ||new File(root,"home/stearlight/.cache/stearlight/os-update/activated.json").isFile();
    try(InputStream in=getAssets().open("quest-overlay.bin")){RootfsArchive.extract(in,root,preserveRuntime);}
    java.nio.file.Files.write(overlayMarker.toPath(),overlayVersion.getBytes(StandardCharsets.US_ASCII));
   }
   syncClock();
   File tmp=new File(getFilesDir(),"tmp"),shm=new File(getFilesDir(),"shm");
   removeTree(tmp,tmp);removeTree(shm,shm);tmp.mkdirs();shm.mkdirs();
   String nativeDir=getApplicationInfo().nativeLibraryDir;
   File logs=new File(getFilesDir(),"logs");logs.mkdirs();
   if(getIntent().getBooleanExtra("developerRuntime",false)){new File(getStateDirectory(),"external-runtime.ready").createNewFile();status("Waiting for the ADB Linux runtime");return;}
   new File(getStateDirectory(),"external-runtime").delete();
   if(closing)return;
   startSocketRegistry(getStateDirectory());
   // OSTree's emulated hardlinks retain absolute Android paths. Android's
   // /data/user/0 and /data/data aliases must resolve in the desktop tracer too.
   File appRepository=new File(root,"home/stearlight/.local/share/flatpak");appRepository.mkdirs();
   List<String> args=new ArrayList<>(Arrays.asList(nativeDir+"/libproot_exec.so","--kill-on-exit","--sysvipc","--link2symlink","-r",root.toString(),"-b","/dev","-b","/proc","-b","/sys","-b",tmp+":/tmp","-b",shm+":/dev/shm","-b",getStateDirectory()+":/run/stearlight/quest","-b",getStateDirectory()+"/proc-net:/proc/net","-w","/home/stearlight","/bin/bash","/opt/quest/start-session.sh"));
   args.addAll(args.indexOf("-w"),Arrays.asList("-b",appRepository+":"+appRepository,
       "-b",appRepository.getCanonicalPath()+":"+appRepository.getCanonicalPath()));
   File pidFile=new File(getFilesDir(),"linux.pid");java.nio.file.Files.deleteIfExists(pidFile.toPath());
   // The shell records its pid and immediately execs PRoot, retaining that pid.
   args.addAll(0,Arrays.asList("/system/bin/sh","-c","printf '%s\\n' \"$$\" > \"$1\" || exit; shift; exec \"$@\"","stearlight-linux",pidFile.toString()));
   ProcessBuilder builder=new ProcessBuilder(args);Map<String,String> env=builder.environment();env.put("LD_LIBRARY_PATH",nativeDir);env.put("PROOT_LOADER",nativeDir+"/libproot_loader.so");env.put("PROOT_TMP_DIR",tmp.toString());env.remove("PROOT_NO_SECCOMP");env.put("PROOT_QUEST_ROBUST","1");env.put("TMPDIR","/tmp");env.put("HOME","/home/stearlight");env.put("USER","stearlight");env.put("LOGNAME","stearlight");env.put("PATH","/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");env.put("GLIBC_TUNABLES","glibc.pthread.rseq=0");
   builder.redirectErrorStream(true);builder.redirectOutput(new File(logs,"linux.log"));
   while(!closing){
    status("Starting Stearlight OS");if(closing)return;
    linux=builder.start();int result=linux.waitFor();if(closing)return;
    // Only the Remote Desktop return asks for a fresh Linux/SteamVR session.
    // PRoot's kill-on-exit has already stopped its owned service processes.
    if(result!=75){status(result==78?"Linux IPC verification failed":"Stearlight stopped ("+result+")");break;}
    status("Returning to Steam");
    removeTree(tmp,tmp);removeTree(shm,shm);tmp.mkdirs();shm.mkdirs();
    startSocketRegistry(getStateDirectory());
   }
  }catch(Exception|LinkageError e){if(closing)return;Log.e("StearlightQuest","Linux startup failed",e);try{status("Stearlight startup failed: "+e.getMessage());}catch(IOException ignored){}}
 }
 private InputStream rootfsStream()throws IOException {List<InputStream> parts=new ArrayList<>();try(BufferedReader list=new BufferedReader(new InputStreamReader(getAssets().open("rootfs.list"),StandardCharsets.US_ASCII))){String name;while((name=list.readLine())!=null){if(!name.matches("rootfs/part-[0-9]{3}\\.bin"))throw new IOException("Invalid rootfs part name");parts.add(getAssets().open(name));}}catch(IOException error){for(InputStream part:parts)part.close();throw error;}if(parts.isEmpty())throw new IOException("Empty rootfs payload");return new SequenceInputStream(Collections.enumeration(parts));}
 private void removeIncomplete(File path)throws IOException {removeTree(path,new File(getFilesDir(),"rootfs-install"));}
 private void removeTree(File path,File base)throws IOException {String target=path.getAbsolutePath();if(!target.equals(base.getAbsolutePath())&&!target.startsWith(base.getAbsolutePath()+File.separator))throw new IOException("Invalid runtime cleanup path");if(java.nio.file.Files.isSymbolicLink(path.toPath())){java.nio.file.Files.delete(path.toPath());return;}if(path.isDirectory()){File[] entries=path.listFiles();if(entries==null)throw new IOException("Cannot read runtime directory");for(File entry:entries)removeTree(entry,base);}if(path.exists()&&!path.delete())throw new IOException("Cannot remove runtime directory");}
 @Override public void onDestroy(){closing=true;if(desktopAppBridge!=null)desktopAppBridge.close();if(usbNetworkBridge!=null)usbNetworkBridge.close();if(audioBridge!=null)audioBridge.close();if(bluetoothBridge!=null)bluetoothBridge.close();unregisterReceiver(batteryReceiver);killLinux();workers.shutdownNow();super.onDestroy();}
}
