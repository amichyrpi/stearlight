package net.stearlight.quest;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.*;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.ParcelUuid;
import android.util.Log;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Iterator;

/** Publish Steam's original BLE advertisement through Android's radio API. */
final class BluetoothBridge implements AutoCloseable {
 private final Context context;
 private final File directory;
 private int lastId;
 private BluetoothLeAdvertiser advertiser;
 private AdvertiseCallback callback;
 private boolean closed;
 BluetoothBridge(Context context,File directory){this.context=context;this.directory=directory;new File(directory,"bluetooth-command.json").delete();new File(directory,"bluetooth-reply.json").delete();}
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
  File command=new File(directory,"bluetooth-command.json");if(!command.isFile())return;
  int id=0;
  try{
   JSONObject request=new JSONObject(new String(Files.readAllBytes(command.toPath()),StandardCharsets.UTF_8));id=request.getInt("id");if(id==lastId)return;lastId=id;
   String operation=request.getString("operation");
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
 @Override public synchronized void close(){closed=true;stop();}
}
