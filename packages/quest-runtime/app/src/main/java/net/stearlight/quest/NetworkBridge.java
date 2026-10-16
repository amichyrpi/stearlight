package net.stearlight.quest;

import android.Manifest;
import android.content.*;
import android.content.pm.PackageManager;
import android.net.*;
import android.net.wifi.*;
import android.provider.Settings;
import android.util.Base64;
import android.util.Log;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/** Publish Android's actual radio state to Steam's NetworkManager client. */
final class NetworkBridge {
 private final QuestActivity activity;
 private final File directory;
 private boolean requestedScanPermission;
 NetworkBridge(QuestActivity activity,File directory){this.activity=activity;this.directory=directory;new File(directory,"network-settings.request").delete();new File(directory,"network-scan.request").delete();}
 private static String encoded(String text){return Base64.encodeToString(text.getBytes(StandardCharsets.UTF_8),Base64.NO_WRAP);}
 private static String ssid(WifiInfo info){String name=info==null?null:info.getSSID();if(name==null||name.equals(WifiManager.UNKNOWN_SSID))return "";return name.startsWith("\"")&&name.endsWith("\"")?name.substring(1,name.length()-1):name;}
 private static int security(String capabilities){return capabilities.contains("PSK")?392:capabilities.contains("EAP")?648:0;}
 private void publishInterfaces(ConnectivityManager cm){
  try{
   // Android blocks the guest's route-netlink dump. Publish the real link
   // properties so libc clients can enumerate Wi-Fi and scoped USB addresses.
   StringBuilder interfaces=new StringBuilder();
   for(Network candidate:cm.getAllNetworks()){
    LinkProperties properties=cm.getLinkProperties(candidate);if(properties==null||properties.getInterfaceName()==null)continue;
    int index=activity.networkInterfaceIndex(properties.getInterfaceName());if(index<=0)continue;
    for(LinkAddress address:properties.getLinkAddresses()){
     String host=address.getAddress().getHostAddress().split("%",2)[0];
     interfaces.append(properties.getInterfaceName()).append(' ').append(index).append(' ')
      .append(address.getAddress() instanceof java.net.Inet6Address?6:4).append(' ').append(address.getPrefixLength()).append(' ').append(host).append('\n');
    }
   }
   int loopback=activity.networkInterfaceIndex("lo");
   if(loopback>0){interfaces.append("lo ").append(loopback).append(" 4 8 127.0.0.1\nlo ").append(loopback).append(" 6 128 ::1\n");}
   File interfacesNext=new File(directory,"network-interfaces.tsv.next");
   try(FileOutputStream out=new FileOutputStream(interfacesNext)){out.write(interfaces.toString().getBytes(StandardCharsets.US_ASCII));}
   if(!interfacesNext.renameTo(new File(directory,"network-interfaces.tsv")))throw new IOException("Cannot publish network interfaces");
  }catch(Exception error){Log.w("StearlightNetwork","Interface state update failed",error);}
 }
 void poll(){
  try{
   WifiManager wifi=(WifiManager)activity.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
   ConnectivityManager cm=(ConnectivityManager)activity.getSystemService(Context.CONNECTIVITY_SERVICE);
   publishInterfaces(cm);
   Network network=cm.getActiveNetwork();NetworkCapabilities caps=cm.getNetworkCapabilities(network);
   boolean connected=caps!=null&&caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
   boolean validated=connected&&caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
   WifiInfo info=wifi==null?null:wifi.getConnectionInfo();
   String name=ssid(info),bssid=info==null?"":info.getBSSID();
   if(bssid==null||bssid.equals("02:00:00:00:00:00"))bssid="";
   StringBuilder text=new StringBuilder();
   text.append("enabled\t").append(wifi!=null&&wifi.isWifiEnabled()?1:0).append('\n');
   text.append("connected\t").append(connected?1:0).append('\n');
   text.append("validated\t").append(validated?1:0).append('\n');
   text.append("current\t").append(encoded(name)).append('\t').append(encoded(bssid)).append('\n');
   Map<String,ScanResult> networks=new TreeMap<>();
   if(wifi!=null&&activity.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED){
    try{for(ScanResult scan:wifi.getScanResults())if(scan.SSID!=null&&!scan.SSID.isEmpty())networks.put(scan.BSSID,scan);}catch(SecurityException ignored){}
   }
   for(ScanResult scan:networks.values())text.append("ap\t").append(encoded(scan.SSID)).append('\t').append(encoded(scan.BSSID)).append('\t').append(WifiManager.calculateSignalLevel(scan.level,101)).append('\t').append(scan.frequency).append('\t').append(security(scan.capabilities)).append('\n');
   if(connected&&(bssid.isEmpty()||!networks.containsKey(bssid))){
    int signal=info==null?0:WifiManager.calculateSignalLevel(info.getRssi(),101);
    int frequency=info==null?0:Math.max(0,info.getFrequency());
    int security=android.os.Build.VERSION.SDK_INT>=31&&info!=null&&info.getCurrentSecurityType()==WifiInfo.SECURITY_TYPE_PSK?392:0;
    text.append("ap\t").append(encoded(name)).append('\t').append(encoded(bssid)).append('\t').append(signal).append('\t').append(frequency).append('\t').append(security).append('\n');
   }
   File next=new File(directory,"network.tsv.next"),target=new File(directory,"network.tsv");
   try(FileOutputStream out=new FileOutputStream(next)){out.write(text.toString().getBytes(StandardCharsets.UTF_8));}
   if(!next.renameTo(target))throw new IOException("Cannot publish network state");
   File scan=new File(directory,"network-scan.request"),settings=new File(directory,"network-settings.request");
   if(scan.isFile()&&scan.delete()){
     if(wifi!=null&&activity.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED)wifi.startScan();
     else if(!requestedScanPermission){requestedScanPermission=true;activity.runOnUiThread(()->activity.requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION},8));}
   }
   if(settings.isFile()&&settings.delete())activity.runOnUiThread(()->{
     try{activity.startActivity(new Intent(Settings.ACTION_WIFI_SETTINGS));Log.i("StearlightNetwork","Opened headset Wi-Fi settings");}
     catch(ActivityNotFoundException error){Log.w("StearlightNetwork","Wi-Fi settings unavailable",error);}
    });
  }catch(Exception error){Log.w("StearlightNetwork","Network state update failed",error);}
 }
}
