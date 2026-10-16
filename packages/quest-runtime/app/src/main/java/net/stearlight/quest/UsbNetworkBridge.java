package net.stearlight.quest;

import android.content.*;
import android.net.*;
import android.util.Log;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;

/** Request Horizon's consent-based USB network; leave Wi-Fi as internet default. */
final class UsbNetworkBridge {
 private final QuestActivity activity;
 private final ConnectivityManager manager;
 private final File directory;
 private final android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
 private ConnectivityManager.NetworkCallback callback;
 private Network network;
 private boolean cable,foreground,closed,lostNetwork;
 private long retryAfter;
 private int speed;
 private final BroadcastReceiver receiver=new BroadcastReceiver(){
  public void onReceive(Context context,Intent intent){
   cable=intent.getBooleanExtra("connected",false);
   if(cable)request();else release();
  }
 };
 UsbNetworkBridge(QuestActivity activity,File directory){
  this.activity=activity;this.directory=directory;
  manager=(ConnectivityManager)activity.getSystemService(Context.CONNECTIVITY_SERVICE);
  Intent current=activity.registerReceiver(receiver,new IntentFilter("android.hardware.usb.action.USB_STATE"));
  if(current!=null)cable=current.getBooleanExtra("connected",false);
 }
 void resumed(boolean value){
  foreground=value;
  if(value){
   if(lostNetwork&&callback!=null&&network==null)release();
   request();
  }
 }
 private void request(){
  if(closed||!foreground||!cable||callback!=null||android.os.SystemClock.elapsedRealtime()<retryAfter)return;
  ConnectivityManager.NetworkCallback next=new ConnectivityManager.NetworkCallback(){
   public void onAvailable(Network value){if(callback==this){network=value;lostNetwork=false;publish("connected");}}
   public void onLinkPropertiesChanged(Network value,LinkProperties properties){if(callback==this){network=value;publish("connected");}}
   public void onCapabilitiesChanged(Network value,NetworkCapabilities caps){if(callback==this){speed=caps.getLinkDownstreamBandwidthKbps();publish("connected");}}
   public void onLost(Network value){
    if(callback!=this||!value.equals(network))return;
    network=null;lostNetwork=true;publish("disconnected");
    // Horizon can leave its provider active after MTP replaces NCM. Give a
    // transient link time to return, then renew the disconnected request.
    ConnectivityManager.NetworkCallback lost=this;
    handler.postDelayed(()->{
     if(!closed&&foreground&&cable&&callback==lost&&network==null){release();request();}
    },1000);
   }
   public void onUnavailable(){if(callback==this){callback=null;network=null;retryAfter=android.os.SystemClock.elapsedRealtime()+30000;publish("unavailable");}}
  };
  callback=next;
  try{
   NetworkRequest request=new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_USB)
    .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    .removeCapability(NetworkCapabilities.NET_CAPABILITY_TRUSTED).build();
   manager.requestNetwork(request,next,handler,30000);publish("requesting");
  }catch(RuntimeException error){callback=null;retryAfter=android.os.SystemClock.elapsedRealtime()+30000;publish("unavailable");Log.w("StearlightUSB","USB network request unavailable",error);}
 }
 private void publish(String status){
  try{
   LinkProperties link=network==null?null:manager.getLinkProperties(network);
   int index=0;String name=link==null?"":link.getInterfaceName();
   if(name!=null&&!name.isEmpty())index=activity.networkInterfaceIndex(name);
   activity.setUsbNetwork(network==null?0:network.getNetworkHandle());
   JSONObject state=new JSONObject();state.put("status",status);state.put("available",network!=null);state.put("interface",name);state.put("scope_id",index);state.put("bandwidth_kbps",speed);
   JSONArray addresses=new JSONArray();if(link!=null)for(LinkAddress address:link.getLinkAddresses())addresses.put(address.toString());state.put("addresses",addresses);
   String ipv4="-";int prefix=0;
   if(link!=null)for(LinkAddress address:link.getLinkAddresses())if(address.getAddress() instanceof java.net.Inet4Address){ipv4=address.getAddress().getHostAddress();prefix=address.getPrefixLength();break;}
   write("usb-network.json",state.toString());
   write("usb-network-scope",index+" "+ipv4+" "+prefix+" "+(network==null?"0":Long.toUnsignedString(network.getNetworkHandle())));
   Log.i("StearlightUSB","USB network "+status+" interface="+name+" scope="+index+" kbps="+speed);
  }catch(Exception error){Log.w("StearlightUSB","Cannot publish USB network state",error);}
 }
 private void write(String name,String text)throws IOException{
  File next=new File(directory,name+".next");try(FileOutputStream out=new FileOutputStream(next)){out.write(text.getBytes(StandardCharsets.UTF_8));}
  if(!next.renameTo(new File(directory,name)))throw new IOException("Cannot publish "+name);
 }
 private void release(){
  if(callback!=null){try{manager.unregisterNetworkCallback(callback);}catch(IllegalArgumentException ignored){}callback=null;}
  network=null;speed=0;lostNetwork=false;publish("disconnected");
 }
 void close(){closed=true;release();activity.unregisterReceiver(receiver);}
}
