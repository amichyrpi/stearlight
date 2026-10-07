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

public final class QuestActivity extends NativeActivity {
 private final ScheduledExecutorService workers=Executors.newScheduledThreadPool(2);
 private volatile java.lang.Process linux;
 private volatile boolean closing;
 private int batteryPercent=-1;
 private boolean charging;
 private BluetoothBridge bluetoothBridge;
 private AudioBridge audioBridge;
 private final BroadcastReceiver batteryReceiver=new BroadcastReceiver(){public void onReceive(Context context,Intent intent){int level=intent.getIntExtra(BatteryManager.EXTRA_LEVEL,-1),scale=intent.getIntExtra(BatteryManager.EXTRA_SCALE,100);batteryPercent=level<0?-1:Math.round(level*100f/Math.max(1,scale));int status=intent.getIntExtra(BatteryManager.EXTRA_STATUS,BatteryManager.BATTERY_STATUS_UNKNOWN);charging=status==BatteryManager.BATTERY_STATUS_CHARGING||status==BatteryManager.BATTERY_STATUS_FULL;}};
 @Override public void onCreate(Bundle state){
  super.onCreate(state);
  registerReceiver(batteryReceiver,new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
  audioBridge=new AudioBridge(this,new File(getStateDirectory()));
  List<String> missing=new ArrayList<>();for(String p:new String[]{Manifest.permission.BLUETOOTH_CONNECT,Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_ADVERTISE,Manifest.permission.RECORD_AUDIO})if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED)missing.add(p);
  if(!missing.isEmpty())requestPermissions(missing.toArray(new String[0]),7);
  workers.scheduleWithFixedDelay(this::writeDeviceState,0,1,TimeUnit.SECONDS);
  bluetoothBridge=new BluetoothBridge(this,new File(getStateDirectory()));
  workers.scheduleWithFixedDelay(bluetoothBridge::poll,0,25,TimeUnit.MILLISECONDS);
  workers.execute(this::startLinux);
 }
 public String getStateDirectory(){File state=new File(getFilesDir(),"state");state.mkdirs();return state.toString();}
 @Override public void onResume(){super.onResume();if(audioBridge!=null)audioBridge.setResumed(true);}
 @Override public void onPause(){if(audioBridge!=null)audioBridge.setResumed(false);super.onPause();}
 public void onXrAudioFocus(boolean focused){runOnUiThread(()->{if(!closing&&audioBridge!=null)audioBridge.setXrFocused(focused);});}
 @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){super.onRequestPermissionsResult(request,permissions,results);if(audioBridge!=null)audioBridge.permissionsChanged();}
 private BootAnimation bootAnimation;
 public android.graphics.Bitmap getBootBitmap(){if(bootAnimation==null)bootAnimation=new BootAnimation(getAssets());return bootAnimation.frame();}
 public boolean bootComplete(){if(bootAnimation==null)bootAnimation=new BootAnimation(getAssets());return bootAnimation.bootComplete();}
 private void writeDeviceState(){
  if(closing)return;
  try {
   JSONObject state=new JSONObject();state.put("battery_percent",batteryPercent);state.put("charging",charging);
   ConnectivityManager cm=(ConnectivityManager)getSystemService(CONNECTIVITY_SERVICE);Network network=cm.getActiveNetwork();NetworkCapabilities caps=cm.getNetworkCapabilities(network);LinkProperties link=cm.getLinkProperties(network);
   state.put("connected",caps!=null&&caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET));state.put("validated",caps!=null&&caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED));state.put("wifi",caps!=null&&caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI));
   JSONArray dns=new JSONArray(),addresses=new JSONArray();if(link!=null){for(java.net.InetAddress address:link.getDnsServers())dns.put(address.getHostAddress());for(LinkAddress address:link.getLinkAddresses())addresses.put(address.toString());}state.put("dns",dns);state.put("addresses",addresses);
   boolean btPermission=checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;BluetoothManager manager=(BluetoothManager)getSystemService(BLUETOOTH_SERVICE);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();state.put("bluetooth_available",adapter!=null);state.put("bluetooth_permission",btPermission);state.put("bluetooth_enabled",btPermission&&adapter!=null&&adapter.isEnabled());state.put("bluetooth_le_advertising",btPermission&&adapter!=null&&adapter.isMultipleAdvertisementSupported());
   JSONArray input=new JSONArray();for(int id:InputDevice.getDeviceIds()){InputDevice device=InputDevice.getDevice(id);if(device==null)continue;android.hardware.BatteryState battery=device.getBatteryState();JSONObject item=new JSONObject();item.put("name",device.getName());item.put("sources",device.getSources());item.put("battery_present",battery.isPresent());item.put("battery_capacity",battery.isPresent()?battery.getCapacity():-1);input.put(item);}state.put("input_device_batteries",input);
   File directory=new File(getStateDirectory()),temporary=new File(directory,"device.json.tmp"),target=new File(directory,"device.json");try(FileOutputStream out=new FileOutputStream(temporary)){out.write(state.toString().getBytes(StandardCharsets.UTF_8));}if(!temporary.renameTo(target))throw new IOException("Cannot publish device state");
   if(link!=null){File root=new File(getFilesDir(),"rootfs"),resolv=new File(root,"etc/resolv.conf");if(resolv.exists()){StringBuilder text=new StringBuilder();for(java.net.InetAddress address:link.getDnsServers())text.append("nameserver ").append(address.getHostAddress()).append('\n');try(FileOutputStream out=new FileOutputStream(resolv)){out.write(text.toString().getBytes(StandardCharsets.US_ASCII));}}}
  }catch(Exception e){Log.w("StearlightQuest","Device state update failed",e);}
 }
 private void status(String value) throws IOException {File dir=new File(getStateDirectory());try(FileOutputStream out=new FileOutputStream(new File(dir,"linux-status"))){out.write(value.getBytes(StandardCharsets.UTF_8));}Log.i("StearlightQuest",value);}
 private void startLinux(){
  try {
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
   try(InputStream in=getAssets().open("quest-overlay.bin")){RootfsArchive.extract(in,root);}
   File tmp=new File(getFilesDir(),"tmp"),shm=new File(getFilesDir(),"shm");tmp.mkdirs();shm.mkdirs();
   String nativeDir=getApplicationInfo().nativeLibraryDir;
   File logs=new File(getFilesDir(),"logs");logs.mkdirs();
   if(new File(getStateDirectory(),"external-runtime").exists()){new File(getStateDirectory(),"external-runtime.ready").createNewFile();status("Waiting for the ADB Linux runtime");return;}
   List<String> args=new ArrayList<>(Arrays.asList(nativeDir+"/libproot_exec.so","--sysvipc","--link2symlink","-r",root.toString(),"-b","/dev","-b","/proc","-b","/sys","-b",tmp+":/tmp","-b",shm+":/dev/shm","-b",getStateDirectory()+":/run/stearlight/quest","-w","/home/stearlight","/bin/bash","/opt/quest/start-session.sh"));
   ProcessBuilder builder=new ProcessBuilder(args);Map<String,String> env=builder.environment();env.put("LD_LIBRARY_PATH",nativeDir);env.put("PROOT_LOADER",nativeDir+"/libproot_loader.so");env.put("PROOT_TMP_DIR",tmp.toString());env.put("PROOT_NO_SECCOMP","1");env.put("TMPDIR",tmp.toString());env.put("HOME","/home/stearlight");env.put("USER","stearlight");env.put("LOGNAME","stearlight");env.put("PATH","/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin");env.put("GLIBC_TUNABLES","glibc.pthread.rseq=0");
   builder.redirectErrorStream(true);builder.redirectOutput(new File(logs,"linux.log"));status("Starting Stearlight OS");linux=builder.start();int result=linux.waitFor();if(!closing)status(result==78?"Linux IPC needs the ADB developer runtime":"Stearlight stopped ("+result+")");
  }catch(Exception e){Log.e("StearlightQuest","Linux startup failed",e);try{status("Stearlight startup failed: "+e.getMessage());}catch(IOException ignored){}}
 }
 private InputStream rootfsStream()throws IOException {List<InputStream> parts=new ArrayList<>();try(BufferedReader list=new BufferedReader(new InputStreamReader(getAssets().open("rootfs.list"),StandardCharsets.US_ASCII))){String name;while((name=list.readLine())!=null){if(!name.matches("rootfs/part-[0-9]{3}\\.bin"))throw new IOException("Invalid rootfs part name");parts.add(getAssets().open(name));}}catch(IOException error){for(InputStream part:parts)part.close();throw error;}if(parts.isEmpty())throw new IOException("Empty rootfs payload");return new SequenceInputStream(Collections.enumeration(parts));}
 private void removeIncomplete(File path)throws IOException {File base=new File(getFilesDir(),"rootfs-install");String target=path.getAbsolutePath();if(!target.equals(base.getAbsolutePath())&&!target.startsWith(base.getAbsolutePath()+File.separator))throw new IOException("Invalid staging path");if(java.nio.file.Files.isSymbolicLink(path.toPath())){java.nio.file.Files.delete(path.toPath());return;}if(path.isDirectory()){File[] entries=path.listFiles();if(entries==null)throw new IOException("Cannot read incomplete installation");for(File entry:entries)removeIncomplete(entry);}if(path.exists()&&!path.delete())throw new IOException("Cannot remove incomplete installation");}
 @Override public void onDestroy(){closing=true;if(audioBridge!=null)audioBridge.close();if(bluetoothBridge!=null)bluetoothBridge.close();unregisterReceiver(batteryReceiver);if(linux!=null)linux.destroy();workers.shutdownNow();super.onDestroy();}
}
