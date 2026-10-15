// Steam and Plasma read UPower; Android remains responsible for battery policy.
// Share the existing services thread and bus, without another Android process.
#include <cmath>
#include <ctime>
#include <variant>
namespace questPower {
static const char* service="org.freedesktop.UPower";
static const char* deviceInterface="org.freedesktop.UPower.Device";
static const std::string root="/org/freedesktop/UPower";
static const std::string battery=root+"/devices/battery_BAT0", display=root+"/devices/DisplayDevice", ac=root+"/devices/line_power_AC";
using Value=std::variant<std::string,bool,uint32_t,uint64_t,int64_t,double>;
using Properties=std::map<std::string,Value>;
static std::map<std::string,Properties> objects;
static std::chrono::steady_clock::time_point nextRead;
static double number(const std::string& text,const char* name,double fallback){
 auto pos=text.find(std::string("\"")+name+"\"");if(pos==std::string::npos)return fallback;
 pos=text.find(':',pos);if(pos==std::string::npos)return fallback;
 char* end=nullptr;auto start=text.c_str()+pos+1;double value=std::strtod(start,&end);
 return end!=start&&std::isfinite(value)?value:fallback;
}
static const char* signature(const Value& v){
 static const char* types[]={"s","b","u","t","x","d"};return types[v.index()];
}
static void append(DBusMessageIter* iter,const Value& value){
 std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;
  if constexpr(std::is_same_v<T,std::string>){const char* p=v.c_str();dbus_message_iter_append_basic(iter,DBUS_TYPE_STRING,&p);}
  else if constexpr(std::is_same_v<T,bool>){dbus_bool_t b=v;dbus_message_iter_append_basic(iter,DBUS_TYPE_BOOLEAN,&b);}
  else dbus_message_iter_append_basic(iter,signature(value)[0],&v);
 },value);
}
static void properties(DBusMessageIter* iter,const Properties& props){
 DBusMessageIter array;dbus_message_iter_open_container(iter,DBUS_TYPE_ARRAY,"{sv}",&array);
 for(const auto& [key,value]:props){
  DBusMessageIter entry,variant;const char* name=key.c_str();
  dbus_message_iter_open_container(&array,DBUS_TYPE_DICT_ENTRY,nullptr,&entry);
  dbus_message_iter_append_basic(&entry,DBUS_TYPE_STRING,&name);
  dbus_message_iter_open_container(&entry,DBUS_TYPE_VARIANT,signature(value),&variant);append(&variant,value);
  dbus_message_iter_close_container(&entry,&variant);dbus_message_iter_close_container(&array,&entry);
 }
 dbus_message_iter_close_container(iter,&array);
}
static std::map<std::string,Properties> model(const std::string& text){
 double percent=number(text,"battery_percent",-1);bool present=percent>=0&&percent<=100;
 unsigned status=number(text,"battery_status",0);
 // Keep compatibility with device.json written by older APKs during upgrades.
 if(!status)status=text.find("\"charging\":true")!=std::string::npos?2:3;
 bool plugged=number(text,"battery_plugged",status==2||status==5?1:0)>0;
 uint32_t state=!present?0:status==2?1:status==3?2:status==5?4:status==4?(plugged?5:6):0;
 uint32_t warning=!present||plugged?1:percent<=5?4:percent<=10?3:1;
 int64_t chargeMs=number(text,"battery_charge_time_ms",-1);
 Properties props={
  {"NativePath",std::string()},{"Vendor",std::string("Meta")},{"Model",std::string("Quest")},{"Serial",std::string()},
  {"Type",uint32_t(2)},{"PowerSupply",true},{"HasHistory",false},{"HasStatistics",false},{"Online",false},
  {"IsPresent",present},{"IsRechargeable",true},{"Percentage",present?percent:0.0},{"State",state},
  {"Voltage",std::max(0.0,number(text,"battery_voltage_mv",0))/1000.0},
  {"Temperature",number(text,"battery_temperature_tenths_c",0)/10.0},
  {"Energy",0.0},{"EnergyEmpty",0.0},{"EnergyFull",0.0},{"EnergyFullDesign",0.0},{"EnergyRate",0.0},
  {"Capacity",0.0},{"Technology",uint32_t(0)},
  {"TimeToEmpty",int64_t(0)},{"TimeToFull",int64_t(state==1&&chargeMs>0?chargeMs/1000:0)},
  {"WarningLevel",warning},{"BatteryLevel",uint32_t(1)},
  {"IconName",std::string(!present?"battery-missing":state==4?"battery-full-charged":state==1?"battery-good-charging":percent<=10?"battery-caution":percent<=30?"battery-low":percent>=90?"battery-full":"battery-good")},
  {"ChargeThresholdSupported",false},{"ChargeThresholdEnabled",false}};
 return {{root,{{"DaemonVersion",std::string("1.90.0")},{"OnBattery",present&&!plugged},{"LidIsClosed",false},{"LidIsPresent",false}}},
         {battery,props},{display,props},{ac,{{"NativePath",std::string()},{"Type",uint32_t(1)},{"PowerSupply",true},{"Online",plugged},{"IsPresent",true},{"Model",std::string("Quest charger")}}}};
}
static void synchronize(DBusConnection* bus,bool force=false){
 auto now=std::chrono::steady_clock::now();if(!force&&now<nextRead)return;nextRead=now+std::chrono::seconds(1);
 auto text=read("device.json");auto next=model(text);
 for(auto& [path,props]:next){
  auto old=objects.find(path);Properties changed;
  for(auto& [name,value]:props)if(old==objects.end()||!old->second.count(name)||old->second.at(name)!=value)changed[name]=value;
  if(path!=root){
   auto timestamp=old==objects.end()||!changed.empty()?Value(uint64_t(std::time(nullptr))):old->second.at("UpdateTime");
   props["UpdateTime"]=timestamp;if(!changed.empty())changed["UpdateTime"]=timestamp;
  }
  if(changed.empty())continue;
  auto message=dbus_message_new_signal(path.c_str(),"org.freedesktop.DBus.Properties","PropertiesChanged");
  DBusMessageIter iter,invalid;dbus_message_iter_init_append(message,&iter);
  const char* iface=path==root?service:deviceInterface;dbus_message_iter_append_basic(&iter,DBUS_TYPE_STRING,&iface);
  properties(&iter,changed);dbus_message_iter_open_container(&iter,DBUS_TYPE_ARRAY,"s",&invalid);dbus_message_iter_close_container(&iter,&invalid);
  dbus_connection_send(bus,message,nullptr);dbus_message_unref(message);
 }
 objects=std::move(next);
}
static bool owns(DBusMessage* request){
 const char* dest=dbus_message_get_destination(request);const char* path=dbus_message_get_path(request);
 return (dest&&!strcmp(dest,service))||(path&&(path==root||std::string(path).find(root+"/")==0));
}
static DBusMessage* handle(DBusConnection* bus,DBusMessage* request){
 const char* path=dbus_message_get_path(request);const char* iface=dbus_message_get_interface(request);const char* method=dbus_message_get_member(request);
 if(!path||!iface||!method)return dbus_message_new_error(request,DBUS_ERROR_INVALID_ARGS,"Expected interface and method");
 if(!objects.count(path))return dbus_message_new_error(request,DBUS_ERROR_UNKNOWN_OBJECT,"Unknown power device");
 auto reply=dbus_message_new_method_return(request);DBusMessageIter out;dbus_message_iter_init_append(reply,&out);
 auto fail=[&](const char* error,const char* detail){dbus_message_unref(reply);return dbus_message_new_error(request,error,detail);};
 const char* expected=path==root?service:deviceInterface;
 if(!strcmp(iface,DBUS_INTERFACE_PROPERTIES)){
  const char* name=nullptr;DBusMessageIter in;
  if(!dbus_message_iter_init(request,&in)||dbus_message_iter_get_arg_type(&in)!=DBUS_TYPE_STRING)return fail(DBUS_ERROR_INVALID_ARGS,"Expected interface");
  dbus_message_iter_get_basic(&in,&name);if(strcmp(name,expected))return fail(DBUS_ERROR_UNKNOWN_INTERFACE,"Unknown power interface");
  const auto& props=objects.at(path);
  if(!strcmp(method,"GetAll"))properties(&out,props);
  else if(!strcmp(method,"Get")){
   const char* key=nullptr;if(!dbus_message_iter_next(&in)||dbus_message_iter_get_arg_type(&in)!=DBUS_TYPE_STRING)return fail(DBUS_ERROR_INVALID_ARGS,"Expected property");
   dbus_message_iter_get_basic(&in,&key);if(!props.count(key))return fail(DBUS_ERROR_UNKNOWN_PROPERTY,"Unknown power property");
   const auto& value=props.at(key);DBusMessageIter variant;dbus_message_iter_open_container(&out,DBUS_TYPE_VARIANT,signature(value),&variant);append(&variant,value);dbus_message_iter_close_container(&out,&variant);
  }else if(!strcmp(method,"Set"))return fail(DBUS_ERROR_PROPERTY_READ_ONLY,"Android manages power settings");
  else return fail(DBUS_ERROR_UNKNOWN_METHOD,"Unknown property method");
 }else if(!strcmp(iface,service)&&path==root&&!strcmp(method,"GetDisplayDevice")){
  const char* value=display.c_str();dbus_message_iter_append_basic(&out,DBUS_TYPE_OBJECT_PATH,&value);
 }else if(!strcmp(iface,service)&&path==root&&!strcmp(method,"EnumerateDevices")){
  DBusMessageIter array;dbus_message_iter_open_container(&out,DBUS_TYPE_ARRAY,"o",&array);
  for(const auto& value:{battery,ac}){const char* p=value.c_str();dbus_message_iter_append_basic(&array,DBUS_TYPE_OBJECT_PATH,&p);}dbus_message_iter_close_container(&out,&array);
 }else if(!strcmp(iface,deviceInterface)&&path!=root&&!strcmp(method,"Refresh"))synchronize(bus,true);
 else if(!strcmp(iface,DBUS_INTERFACE_PEER)&&!strcmp(method,"Ping")){}
 else if(!strcmp(iface,DBUS_INTERFACE_INTROSPECTABLE)&&!strcmp(method,"Introspect")){
  std::string xml="<node><interface name='org.freedesktop.DBus.Properties'><method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method><method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method><signal name='PropertiesChanged'><arg type='s'/><arg type='a{sv}'/><arg type='as'/></signal></interface><interface name='"+std::string(expected)+"'>";
  for(const auto& [name,value]:objects.at(path))xml+="<property name='"+name+"' type='"+signature(value)+"' access='read'/>";
  if(path==root)xml+="<method name='GetDisplayDevice'><arg type='o' direction='out'/></method><method name='EnumerateDevices'><arg type='ao' direction='out'/></method>";
  else xml+="<method name='Refresh'/>";
  xml+="</interface></node>";const char* p=xml.c_str();dbus_message_iter_append_basic(&out,DBUS_TYPE_STRING,&p);
 }else return fail(DBUS_ERROR_UNKNOWN_METHOD,"Power operation unavailable on Android");
 return reply;
}
}
