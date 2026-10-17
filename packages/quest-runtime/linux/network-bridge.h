// Android owns Wi-Fi; expose its observations through NetworkManager's D-Bus API.
// Runs in the existing radio bridge, avoiding another Android child process.
#include <functional>
#include <iomanip>
namespace questNetwork {
static const std::string root="/org/freedesktop/NetworkManager";
static const std::string device=root+"/Devices/1", active=root+"/ActiveConnection/1";
static const char* manager="org.freedesktop.NetworkManager";
struct Value { std::string signature,text; uint64_t number=0; std::vector<std::string> list; };
using Properties=std::map<std::string,Value>;
using Interfaces=std::map<std::string,Properties>;
using Objects=std::map<std::string,Interfaces>;
static Objects objects;
static std::string previous;
static Value s(std::string text){return {"s",text,0,{}};}
static Value o(std::string text){return {"o",text,0,{}};}
static Value u(unsigned value){return {"u","",value,{}};}
static Value b(bool value){return {"b","",value,{}};}
static Value paths(std::vector<std::string> value){return {"ao","",0,value};}
static std::string decode(const std::string& text){
 std::string output;unsigned bits=0,count=0;
 const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 for(char ch:text){auto n=alphabet.find(ch);if(n==std::string::npos)break;bits=(bits<<6)|n;count+=6;if(count>=8){count-=8;output+=char(bits>>count);}}
 return output;
}
static void append(DBusMessageIter* iter,const Value& value){
 if(value.signature=="s"||value.signature=="o"){const char* data=value.text.c_str();dbus_message_iter_append_basic(iter,value.signature=="o"?DBUS_TYPE_OBJECT_PATH:DBUS_TYPE_STRING,&data);}
 else if(value.signature=="b"){dbus_bool_t data=value.number!=0;dbus_message_iter_append_basic(iter,DBUS_TYPE_BOOLEAN,&data);}
 else if(value.signature=="u"){dbus_uint32_t data=value.number;dbus_message_iter_append_basic(iter,DBUS_TYPE_UINT32,&data);}
 else if(value.signature=="y"){unsigned char data=value.number;dbus_message_iter_append_basic(iter,DBUS_TYPE_BYTE,&data);}
 else if(value.signature=="x"){dbus_int64_t data=value.number;dbus_message_iter_append_basic(iter,DBUS_TYPE_INT64,&data);}
 else if(value.signature=="(uu)"){DBusMessageIter tuple;dbus_message_iter_open_container(iter,DBUS_TYPE_STRUCT,nullptr,&tuple);dbus_uint32_t data=value.number,reason=0;dbus_message_iter_append_basic(&tuple,DBUS_TYPE_UINT32,&data);dbus_message_iter_append_basic(&tuple,DBUS_TYPE_UINT32,&reason);dbus_message_iter_close_container(iter,&tuple);}
 else {
  DBusMessageIter array;dbus_message_iter_open_container(iter,DBUS_TYPE_ARRAY,value.signature.c_str()+1,&array);
  if(value.signature=="ay"){for(unsigned char ch:value.text)dbus_message_iter_append_basic(&array,DBUS_TYPE_BYTE,&ch);}
  else if(value.signature=="ao"){for(auto& entry:value.list){const char* data=entry.c_str();dbus_message_iter_append_basic(&array,DBUS_TYPE_OBJECT_PATH,&data);}}
  dbus_message_iter_close_container(iter,&array);
 }
}
static void properties(DBusMessageIter* iter,const Properties& values){
 DBusMessageIter array;dbus_message_iter_open_container(iter,DBUS_TYPE_ARRAY,"{sv}",&array);
 for(auto& [name,value]:values){
  DBusMessageIter entry,variant;const char* key=name.c_str();
  dbus_message_iter_open_container(&array,DBUS_TYPE_DICT_ENTRY,nullptr,&entry);
  dbus_message_iter_append_basic(&entry,DBUS_TYPE_STRING,&key);
  dbus_message_iter_open_container(&entry,DBUS_TYPE_VARIANT,value.signature.c_str(),&variant);append(&variant,value);
  dbus_message_iter_close_container(&entry,&variant);dbus_message_iter_close_container(&array,&entry);
 }
 dbus_message_iter_close_container(iter,&array);
}
static void interfaces(DBusMessageIter* iter,const Interfaces& values){
 DBusMessageIter array;dbus_message_iter_open_container(iter,DBUS_TYPE_ARRAY,"{sa{sv}}",&array);
 for(auto& [name,props]:values){DBusMessageIter entry;const char* key=name.c_str();dbus_message_iter_open_container(&array,DBUS_TYPE_DICT_ENTRY,nullptr,&entry);dbus_message_iter_append_basic(&entry,DBUS_TYPE_STRING,&key);properties(&entry,props);dbus_message_iter_close_container(&array,&entry);}
 dbus_message_iter_close_container(iter,&array);
}
static void command(const char* operation){
 const std::string path=std::string(directory)+(strcmp(operation,"settings")?"network-scan.request":"network-settings.request");
 std::ofstream out(path+".next");out<<operation<<'\n';out.close();rename((path+".next").c_str(),path.c_str());
}
static Objects model(const std::string& text){
 bool enabled=false,connected=false,validated=false;std::string ssid,bssid,line;
 std::vector<std::vector<std::string>> networks;
 std::istringstream input(text);
 while(std::getline(input,line)){
  std::vector<std::string> fields;std::istringstream row(line);std::string field;
  while(std::getline(row,field,'\t'))fields.push_back(field);
  if(fields.size()<2)continue;
  if(fields[0]=="enabled")enabled=fields[1]=="1";
  if(fields[0]=="connected")connected=fields[1]=="1";
  if(fields[0]=="validated")validated=fields[1]=="1";
  if(fields[0]=="current"){ssid=decode(fields[1]);if(fields.size()>2)bssid=decode(fields[2]);}
  if(fields[0]=="ap"&&fields.size()==6)networks.push_back(fields);
 }
 Objects result;std::vector<std::string> aps;std::string current="/";
 for(const auto& ap:networks){
  std::string name=decode(ap[1]),address=decode(ap[2]);
  // A stable path preserves Steam's access-point identity across signal updates.
  uint64_t id=1469598103934665603ULL;for(unsigned char ch:address+name){id^=ch;id*=1099511628211ULL;}
  std::ostringstream hex;hex<<std::hex<<id;std::string path=root+"/AccessPoint/ap"+hex.str();aps.push_back(path);
  if(connected&&address==bssid&&name==ssid)current=path;
  unsigned security=std::strtoul(ap[5].c_str(),nullptr,10);
  result[path]["org.freedesktop.NetworkManager.AccessPoint"]={
   {"Ssid",{"ay",name,0,{}}},{"HwAddress",s(address)},{"Strength",{"y","",std::min(100UL,std::strtoul(ap[3].c_str(),nullptr,10)),{}}},
   {"Frequency",u(std::strtoul(ap[4].c_str(),nullptr,10))},{"Mode",u(2)},{"Flags",u(security?1:0)},
   {"WpaFlags",u(0)},{"RsnFlags",u(security)},{"MaxBitrate",u(0)},{"LastSeen",{"x","",0,{}}}};
 }
 result[root][manager]={
  {"Version",s("1.42.0")},{"State",u(validated?70:connected?60:20)},{"Connectivity",u(validated?4:connected?3:1)},
  {"NetworkingEnabled",b(true)},{"WirelessEnabled",b(enabled)},{"WirelessHardwareEnabled",b(true)},
  {"WwanEnabled",b(false)},{"WwanHardwareEnabled",b(false)},{"Startup",b(false)},
  {"Devices",paths({device})},{"AllDevices",paths({device})},{"ActiveConnections",paths(connected?std::vector<std::string>{active}:std::vector<std::string>{})},
  {"PrimaryConnection",o(connected?active:"/")},{"PrimaryConnectionType",s(connected?"802-11-wireless":"")},{"ActivatingConnection",o("/")}};
 result[device]["org.freedesktop.NetworkManager.Device"]={
  {"Interface",s("wlan0")},{"IpInterface",s("wlan0")},{"Driver",s("android")},{"Managed",b(true)},{"Autoconnect",b(true)},
  {"DeviceType",u(2)},{"Real",b(true)},{"State",u(connected?100:enabled?30:20)},{"StateReason",{"(uu)","",connected?100ULL:enabled?30ULL:20ULL,{}}},{"Ip4Config",o("/")},{"Ip6Config",o("/")},
  {"ActiveConnection",o(connected?active:"/")},{"AvailableConnections",paths({})},{"FirmwareMissing",b(false)},{"NmPluginMissing",b(false)}};
 result[device]["org.freedesktop.NetworkManager.Device.Wireless"]={
  {"HwAddress",s("")},{"PermHwAddress",s("")},{"Mode",u(2)},{"Bitrate",u(0)},{"WirelessCapabilities",u(63)},
  {"AccessPoints",paths(aps)},{"ActiveAccessPoint",o(current)},{"LastScan",{"x","",0,{}}}};
 result[root+"/Settings"]["org.freedesktop.NetworkManager.Settings"]={{"Connections",paths({})},{"CanModify",b(false)},{"Hostname",s("frame")}};
 if(connected)result[active]["org.freedesktop.NetworkManager.Connection.Active"]={
  {"Id",s(ssid.empty()?"Wi-Fi":ssid)},{"Uuid",s("")},{"Type",s("802-11-wireless")},{"Connection",o("/")},
  {"SpecificObject",o(current)},{"Devices",paths({device})},{"State",u(2)},{"Default",b(true)},{"Default6",b(true)},
  {"Ip4Config",o("/")},{"Ip6Config",o("/")},{"Vpn",b(false)}};
 return result;
}
static void synchronize(DBusConnection* bus){
 auto text=read("network.tsv");if(text.empty()||text==previous)return;previous=text;
 auto next=model(text);
 for(auto& [path,ifaces]:next){
  auto added=objects.find(path)==objects.end();
  if(added){
   auto message=dbus_message_new_signal(root.c_str(),"org.freedesktop.DBus.ObjectManager","InterfacesAdded");
   DBusMessageIter iter;dbus_message_iter_init_append(message,&iter);const char* object=path.c_str();dbus_message_iter_append_basic(&iter,DBUS_TYPE_OBJECT_PATH,&object);interfaces(&iter,ifaces);dbus_connection_send(bus,message,nullptr);dbus_message_unref(message);
  } else for(auto& [name,props]:ifaces){
   auto message=dbus_message_new_signal(path.c_str(),"org.freedesktop.DBus.Properties","PropertiesChanged");
   DBusMessageIter iter,invalidated;dbus_message_iter_init_append(message,&iter);const char* iface=name.c_str();dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&iface);properties(&iter,props);dbus_message_iter_open_container(&iter,DBUS_TYPE_ARRAY,"s",&invalidated);dbus_message_iter_close_container(&iter,&invalidated);dbus_connection_send(bus,message,nullptr);dbus_message_unref(message);
  }
 }
 for(auto& [path,ifaces]:objects)if(!next.count(path)){
  auto message=dbus_message_new_signal(root.c_str(),"org.freedesktop.DBus.ObjectManager","InterfacesRemoved");
  DBusMessageIter iter,array;dbus_message_iter_init_append(message,&iter);const char* object=path.c_str();dbus_message_iter_append_basic(&iter,DBUS_TYPE_OBJECT_PATH,&object);dbus_message_iter_open_container(&iter,DBUS_TYPE_ARRAY,"s",&array);for(auto& [name,props]:ifaces){const char* iface=name.c_str();dbus_message_iter_append_basic(&array,DBUS_TYPE_STRING,&iface);}dbus_message_iter_close_container(&iter,&array);dbus_connection_send(bus,message,nullptr);dbus_message_unref(message);
 }
 objects=std::move(next);
}
static bool owns(DBusMessage* request){const char* dest=dbus_message_get_destination(request);const char* path=dbus_message_get_path(request);return (dest&&!strcmp(dest,manager))||(path&&(std::string(path).find(root)==0||!strcmp(path,"/org/freedesktop")));}
static DBusMessage* handle(DBusMessage* request){
 const char* member=dbus_message_get_member(request);const char* path=dbus_message_get_path(request);const char* iface=dbus_message_get_interface(request);
 if(!member||!path||!iface)return nullptr;
 auto reply=dbus_message_new_method_return(request);DBusMessageIter output;dbus_message_iter_init_append(reply,&output);
 auto fail=[&](const char* message){dbus_message_unref(reply);return dbus_message_new_error(request,"org.freedesktop.NetworkManager.NotSupported",message);};
 if(!strcmp(member,"GetManagedObjects")){
  DBusMessageIter array;dbus_message_iter_open_container(&output,DBUS_TYPE_ARRAY,"{oa{sa{sv}}}",&array);
  for(auto& [name,ifaces]:objects){DBusMessageIter entry;const char* object=name.c_str();dbus_message_iter_open_container(&array,DBUS_TYPE_DICT_ENTRY,nullptr,&entry);dbus_message_iter_append_basic(&entry,DBUS_TYPE_OBJECT_PATH,&object);interfaces(&entry,ifaces);dbus_message_iter_close_container(&array,&entry);}dbus_message_iter_close_container(&output,&array);
 }else if(!strcmp(iface,"org.freedesktop.DBus.Properties")){
  const char* name=nullptr;DBusMessageIter input;if(!dbus_message_iter_init(request,&input)||dbus_message_iter_get_arg_type(&input)!=DBUS_TYPE_STRING)return fail("Expected interface");dbus_message_iter_get_basic(&input,&name);
  if(!objects.count(path)||!objects[path].count(name))return fail("Unknown network interface");
  const auto& props=objects[path][name];
  if(!strcmp(member,"GetAll"))properties(&output,props);
  else if(!strcmp(member,"Get")){
   const char* key=nullptr;if(!dbus_message_iter_next(&input)||dbus_message_iter_get_arg_type(&input)!=DBUS_TYPE_STRING)return fail("Expected property");dbus_message_iter_get_basic(&input,&key);if(!props.count(key))return fail("Unknown network property");
   const auto& value=props.at(key);DBusMessageIter variant;dbus_message_iter_open_container(&output,DBUS_TYPE_VARIANT,value.signature.c_str(),&variant);append(&variant,value);dbus_message_iter_close_container(&output,&variant);
  }else if(!strcmp(member,"Set")){
   const char* key=nullptr;if(!dbus_message_iter_next(&input)||dbus_message_iter_get_arg_type(&input)!=DBUS_TYPE_STRING)return fail("Expected property");dbus_message_iter_get_basic(&input,&key);
   if(strcmp(name,manager)||strcmp(key,"WirelessEnabled"))return fail("This setting is managed by the headset");
   if(!dbus_message_iter_next(&input)||dbus_message_iter_get_arg_type(&input)!=DBUS_TYPE_VARIANT)return fail("Expected Wi-Fi switch value");
   DBusMessageIter value;dbus_message_iter_recurse(&input,&value);if(dbus_message_iter_get_arg_type(&value)!=DBUS_TYPE_BOOLEAN)return fail("Expected Wi-Fi switch value");
   dbus_bool_t desired;dbus_message_iter_get_basic(&value,&desired);
   if(bool(desired)!=bool(props.at(key).number)){command("settings");previous.clear();}
  }
  else return fail("Unknown property method");
 }else if(!strcmp(member,"GetDevices")||!strcmp(member,"GetAllDevices"))append(&output,paths({device}));
 else if(!strcmp(member,"GetAllAccessPoints")||!strcmp(member,"GetAccessPoints"))append(&output,objects[device]["org.freedesktop.NetworkManager.Device.Wireless"]["AccessPoints"]);
 else if(!strcmp(member,"ListConnections"))append(&output,paths({}));
 else if(!strcmp(member,"CheckConnectivity"))append(&output,objects[root][manager]["Connectivity"]);
 else if(!strcmp(member,"state"))append(&output,objects[root][manager]["State"]);
 else if(!strcmp(member,"RequestScan"))command("scan");
 else if(!strcmp(member,"GetPermissions")){DBusMessageIter array;dbus_message_iter_open_container(&output,DBUS_TYPE_ARRAY,"{ss}",&array);dbus_message_iter_close_container(&output,&array);}
 else if(!strcmp(member,"ActivateConnection")||!strcmp(member,"AddAndActivateConnection")||!strcmp(member,"AddAndActivateConnection2")||!strcmp(member,"DeactivateConnection")||!strcmp(member,"Disconnect")||!strcmp(member,"Enable")){command("settings");return fail("Choose your network in the headset's Wi-Fi settings");}
 else return fail("Network method unavailable on Android");
 return reply;
}
}
