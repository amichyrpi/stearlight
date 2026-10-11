package net.stearlight.quest;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.os.ParcelUuid;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/** Steam's BlueZ API uses Android's radio, discovery and bonding services. */
final class BluetoothBridge implements AutoCloseable {
 private final Context context;
 private final File directory;
 private int lastId;
 private BluetoothLeAdvertiser advertiser;
 private AdvertiseCallback callback;
 private boolean closed;
 private boolean discovering;
 private long scanUntil,nextPublish;
 private BluetoothDevice connectionSettingsDevice;
 private long connectionSettingsAfter;
 private BluetoothLeScanner scanner;
 private final Map<String,BluetoothDevice> devices=new TreeMap<>();
 private final Map<String,Integer> levels=new HashMap<>();
 private final Set<String> connected=new HashSet<>();
 private final Set<String> resolving=new HashSet<>();
 private final Map<Integer,BluetoothProfile> profiles=new HashMap<>();
 private final ScanCallback scanCallback=new ScanCallback(){
  @Override public void onScanResult(int type,ScanResult result){synchronized(BluetoothBridge.this){if(!result.isConnectable())return;remember(result.getDevice());levels.put(result.getDevice().getAddress(),result.getRssi());}}
  @Override public void onScanFailed(int code){Log.e("StearlightBluetooth","BLE discovery failed: "+code);}
 };
 private final BroadcastReceiver receiver=new BroadcastReceiver(){
  @Override public void onReceive(Context ignored,Intent intent){synchronized(BluetoothBridge.this){
   if(closed)return;
   BluetoothDevice device=intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
   if(device!=null){remember(device);String address=device.getAddress();
    if(BluetoothDevice.ACTION_FOUND.equals(intent.getAction()))levels.put(address,(int)intent.getShortExtra(BluetoothDevice.EXTRA_RSSI,(short)-127));
    if(BluetoothDevice.ACTION_UUID.equals(intent.getAction())){Log.i("StearlightBluetooth","Device service lookup completed: "+device.getName());if(connectionSettingsDevice!=null&&connectionSettingsDevice.equals(device))connectionSettingsAfter=0;}
    if(BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(intent.getAction())){resolving.remove(address);if(device.getBondState()==BluetoothDevice.BOND_BONDED)resolveServices(device);}
    if(intent.getAction().endsWith("profile.action.CONNECTION_STATE_CHANGED")){
     int state=intent.getIntExtra(BluetoothProfile.EXTRA_STATE,BluetoothProfile.STATE_DISCONNECTED);
     if(state==BluetoothProfile.STATE_CONNECTED)connected.add(address);
     if(state==BluetoothProfile.STATE_DISCONNECTED)connected.remove(address);
    }
    if(BluetoothDevice.ACTION_ACL_DISCONNECTED.equals(intent.getAction()))connected.remove(address);
   }
   publish();
  }}
 };
 BluetoothBridge(Context context,File directory){
  this.context=context;this.directory=directory;new File(directory,"bluetooth-command.json").delete();new File(directory,"bluetooth-reply.json").delete();
  IntentFilter filter=new IntentFilter();for(String action:new String[]{BluetoothDevice.ACTION_FOUND,BluetoothDevice.ACTION_BOND_STATE_CHANGED,BluetoothDevice.ACTION_NAME_CHANGED,BluetoothDevice.ACTION_UUID,BluetoothDevice.ACTION_ACL_DISCONNECTED,BluetoothAdapter.ACTION_DISCOVERY_FINISHED,android.bluetooth.BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED,android.bluetooth.BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED,"android.bluetooth.input.profile.action.CONNECTION_STATE_CHANGED"})filter.addAction(action);
  context.registerReceiver(receiver,filter);
 }
 private BluetoothAdapter adapter(){BluetoothManager manager=(BluetoothManager)context.getSystemService(Context.BLUETOOTH_SERVICE);return manager==null?null:manager.getAdapter();}
 private void remember(BluetoothDevice device){if(device!=null)devices.put(device.getAddress(),device);}
 private void resolveServices(BluetoothDevice device){
  ParcelUuid[] services=device.getUuids();BluetoothAdapter radio=adapter();
  if((services!=null&&services.length>0)||radio==null||!radio.isEnabled()||!resolving.add(device.getAddress()))return;
  if(device.fetchUuidsWithSdp())Log.i("StearlightBluetooth","Requested paired device services: "+device.getName());
  else Log.w("StearlightBluetooth","Paired device service lookup rejected: "+device.getName());
 }
 private void initializeProfiles(BluetoothAdapter radio){
  if(!profiles.isEmpty())return;
  // Android owns these profiles; HID host (4) includes keyboards and gamepads.
  for(int profile:new int[]{BluetoothProfile.A2DP,BluetoothProfile.HEADSET,4}){
   profiles.put(profile,null);
   radio.getProfileProxy(context,new BluetoothProfile.ServiceListener(){
    public void onServiceConnected(int type,BluetoothProfile proxy){synchronized(BluetoothBridge.this){if(closed){radio.closeProfileProxy(type,proxy);return;}profiles.put(type,proxy);publish();}}
    public void onServiceDisconnected(int type){synchronized(BluetoothBridge.this){profiles.put(type,null);publish();}}
   },profile);
  }
 }
 private static String field(String value){return value==null?"":value.replace('\t',' ').replace('\r',' ').replace('\n',' ');}
 private void publish(){
  try{
   BluetoothAdapter radio=adapter();boolean permitted=context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED;
   if(permitted&&radio!=null){for(BluetoothDevice device:radio.getBondedDevices()){remember(device);resolveServices(device);}initializeProfiles(radio);}
   Set<String> linked=new HashSet<>(connected);for(BluetoothProfile profile:profiles.values())if(profile!=null)for(BluetoothDevice device:profile.getConnectedDevices()){remember(device);linked.add(device.getAddress());}
   StringBuilder data=new StringBuilder("adapter\t").append(discovering?1:0).append('\n');
   if(permitted)for(BluetoothDevice device:devices.values()){
    String address=device.getAddress(),name=device.getName();if(name==null||name.isEmpty())name=address;
    android.bluetooth.BluetoothClass cls=device.getBluetoothClass();StringJoiner uuids=new StringJoiner(",");ParcelUuid[] ids=device.getUuids();if(ids!=null)for(ParcelUuid uuid:ids)uuids.add(uuid.toString());
    data.append(address).append('\t').append(field(name)).append('\t').append(device.getBondState()).append('\t').append(linked.contains(address)?1:0).append('\t').append(cls==null?0:cls.getDeviceClass()).append('\t').append(levels.getOrDefault(address,-127)).append('\t').append(uuids).append('\n');
   }
   File temporary=new File(directory,"bluetooth-devices.tmp");try(FileOutputStream out=new FileOutputStream(temporary)){out.write(data.toString().getBytes(StandardCharsets.UTF_8));}if(!temporary.renameTo(new File(directory,"bluetooth-devices.tsv")))throw new IOException("Cannot publish Bluetooth devices");
  }catch(Exception e){Log.w("StearlightBluetooth","Device snapshot failed",e);}
 }
 private void stopDiscovery(){
  BluetoothAdapter radio=adapter();if(radio!=null&&context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED){if(discovering&&radio.isDiscovering())radio.cancelDiscovery();if(scanner!=null)scanner.stopScan(scanCallback);}scanner=null;discovering=false;publish();
 }
 private void openConnectionSettings(){
  if(connectionSettingsDevice==null||android.os.SystemClock.elapsedRealtime()<connectionSettingsAfter)return;
  BluetoothDevice device=connectionSettingsDevice;connectionSettingsDevice=null;
  new Handler(Looper.getMainLooper()).post(()->{try{context.startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS));}catch(RuntimeException e){Log.e("StearlightBluetooth","Cannot open Bluetooth connection settings",e);}});
  Log.i("StearlightBluetooth","System Bluetooth connection requested: "+device.getName());
 }
 private void discover()throws Exception{
  if(context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)!=PackageManager.PERMISSION_GRANTED)throw new SecurityException("Nearby devices permission missing");
  BluetoothAdapter radio=adapter();if(radio==null||!radio.isEnabled())throw new IllegalStateException("Bluetooth radio is disabled");
  stopDiscovery();discovering=true;scanUntil=android.os.SystemClock.elapsedRealtime()+30000;
  boolean classic=radio.startDiscovery();scanner=radio.getBluetoothLeScanner();if(scanner!=null)scanner.startScan(null,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
  if(!classic&&scanner==null){discovering=false;throw new IOException("Android rejected Bluetooth discovery");}publish();Log.i("StearlightBluetooth","Classic and BLE discovery started");
 }
 private void deviceOperation(String operation,String address)throws Exception{
  BluetoothAdapter radio=adapter();if(radio==null||!BluetoothAdapter.checkBluetoothAddress(address))throw new IllegalArgumentException("Invalid Bluetooth address");BluetoothDevice device=radio.getRemoteDevice(address);remember(device);stopDiscovery();
  if(operation.equals("pair")){if(device.getBondState()==BluetoothDevice.BOND_BONDED)return;if(!device.createBond())throw new IOException("Android rejected pairing");publish();return;}
  if(operation.equals("forget")||operation.equals("cancel-pair")){
   if(operation.equals("forget")&&device.getBondState()==BluetoothDevice.BOND_NONE)return;
   if(operation.equals("cancel-pair")&&device.getBondState()!=BluetoothDevice.BOND_BONDING)return;
   if(!Boolean.TRUE.equals(device.getClass().getMethod(operation.equals("forget")?"removeBond":"cancelBondProcess").invoke(device)))throw new IOException("Android rejected the bond operation");publish();return;
  }
  boolean link=operation.equals("connect");if(link&&connected.contains(address))return;
  if(link&&device.getBondState()!=BluetoothDevice.BOND_BONDED)throw new IOException("Pair the device first");
  boolean accepted=false;for(BluetoothProfile profile:profiles.values())if(profile!=null)try{
   if(link&&profile.getConnectedDevices().contains(device)){accepted=true;continue;}
   Object result=profile.getClass().getMethod(link?"connect":"disconnect",BluetoothDevice.class).invoke(profile,device);accepted|=Boolean.TRUE.equals(result);
  }catch(ReflectiveOperationException e){Log.d("StearlightBluetooth","Profile operation unavailable: "+profile.getClass().getSimpleName());}
  if(!accepted){
   if(!link)throw new IOException("Android has not accepted this device profile disconnection");
   resolving.remove(address);resolveServices(device);
   // HID/audio profile connection is privileged on Horizon OS. Its Settings UI
   // performs that operation; Steam's reply still waits for a real profile link.
   if(context.getPackageManager().resolveActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS),0)==null)throw new IOException("Bluetooth connection settings are unavailable");
   connectionSettingsDevice=device;ParcelUuid[] services=device.getUuids();
   connectionSettingsAfter=android.os.SystemClock.elapsedRealtime()+(services!=null&&services.length>0?0:10000);
  }publish();
 }
 private static byte[] bytes(JSONArray array)throws JSONException{
  byte[] data=new byte[array.length()];for(int i=0;i<data.length;i++){int value=array.getInt(i);if(value<0||value>255)throw new JSONException("Invalid BLE data byte");data[i]=(byte)value;}return data;
 }
 private void reply(int id,boolean success,String error){
  try{
   JSONObject value=new JSONObject();value.put("id",id);value.put("success",success);value.put("error",error);
   File temporary=new File(directory,"bluetooth-reply.tmp");try(FileOutputStream out=new FileOutputStream(temporary)){out.write(value.toString().getBytes(StandardCharsets.UTF_8));}
   if(!temporary.renameTo(new File(directory,"bluetooth-reply.json")))throw new IOException("Cannot publish BLE reply");
  }catch(Exception e){Log.e("StearlightBluetooth","BLE reply failed",e);}
 }
 synchronized void poll(){
  if(closed)return;
  long now=android.os.SystemClock.elapsedRealtime();if(discovering&&now>=scanUntil)stopDiscovery();if(now>=nextPublish){nextPublish=now+1000;publish();}
  openConnectionSettings();
  File command=new File(directory,"bluetooth-command.json");if(!command.isFile())return;
  int id=0;
  try{
   JSONObject request=new JSONObject(new String(Files.readAllBytes(command.toPath()),StandardCharsets.UTF_8));id=request.getInt("id");if(id==lastId)return;lastId=id;
   String operation=request.getString("operation");
   if(operation.equals("discovery-start")){discover();reply(id,true,"");return;}
   if(operation.equals("discovery-stop")){stopDiscovery();reply(id,true,"");return;}
   if(operation.equals("pair")||operation.equals("connect")||operation.equals("disconnect")||operation.equals("forget")||operation.equals("cancel-pair")){deviceOperation(operation,request.getJSONObject("properties").getString("address"));reply(id,true,"");return;}
   if(operation.equals("stop")){stop();reply(id,true,"");return;}
   if(!operation.equals("start"))throw new IllegalArgumentException("Unsupported radio operation");
   if(context.checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE)!=PackageManager.PERMISSION_GRANTED||context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)throw new SecurityException("Bluetooth permission missing");
   BluetoothManager manager=(BluetoothManager)context.getSystemService(Context.BLUETOOTH_SERVICE);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
   if(adapter==null||!adapter.isEnabled())throw new IllegalStateException("Bluetooth radio is disabled");
   if(!adapter.isMultipleAdvertisementSupported())throw new IllegalStateException("BLE advertising is unavailable");
   if(callback!=null)throw new IllegalStateException("An advertisement is already active");
   advertiser=adapter.getBluetoothLeAdvertiser();if(advertiser==null)throw new IllegalStateException("Bluetooth advertiser is unavailable");
   JSONObject properties=request.getJSONObject("properties");AdvertiseData.Builder data=new AdvertiseData.Builder();
   JSONArray uuids=properties.optJSONArray("ServiceUUIDs");if(uuids!=null)for(int i=0;i<uuids.length();i++)data.addServiceUuid(ParcelUuid.fromString(uuids.getString(i)));
   JSONObject services=properties.optJSONObject("ServiceData");if(services!=null)for(Iterator<String> keys=services.keys();keys.hasNext();){String key=keys.next();data.addServiceData(ParcelUuid.fromString(key),bytes(services.getJSONArray(key)));}
   JSONObject manufacturers=properties.optJSONObject("ManufacturerData");if(manufacturers!=null)for(Iterator<String> keys=manufacturers.keys();keys.hasNext();){String key=keys.next();data.addManufacturerData(Integer.parseInt(key),bytes(manufacturers.getJSONArray(key)));}
   JSONArray includes=properties.optJSONArray("Includes");if(includes!=null)for(int i=0;i<includes.length();i++)if(includes.getString(i).equals("tx-power"))data.setIncludeTxPowerLevel(true);
   boolean connectable=properties.optString("Type","broadcast").equals("peripheral");
   AdvertiseSettings settings=new AdvertiseSettings.Builder().setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY).setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM).setConnectable(connectable).build();
   final int commandId=id;
   callback=new AdvertiseCallback(){
    @Override public void onStartSuccess(AdvertiseSettings settings){Log.i("StearlightBluetooth","Steam BLE advertisement active");reply(commandId,true,"");}
    @Override public void onStartFailure(int code){synchronized(BluetoothBridge.this){callback=null;}Log.e("StearlightBluetooth","BLE advertising rejected: "+code);reply(commandId,false,"Advertising error "+code);}
   };
   advertiser.startAdvertising(settings,data.build(),callback);
  }catch(Exception e){Log.e("StearlightBluetooth","BLE request failed",e);reply(id,false,e.getMessage());}
 }
 private void stop(){if(advertiser!=null&&callback!=null)advertiser.stopAdvertising(callback);callback=null;advertiser=null;}
 @Override public synchronized void close(){closed=true;stopDiscovery();stop();context.unregisterReceiver(receiver);BluetoothAdapter radio=adapter();if(radio!=null)for(Map.Entry<Integer,BluetoothProfile> entry:profiles.entrySet())if(entry.getValue()!=null)radio.closeProfileProxy(entry.getKey(),entry.getValue());profiles.clear();}
}
