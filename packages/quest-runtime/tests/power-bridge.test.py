"""Exercise Steam's UPower contract against changing Android battery readings."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HARNESS = r'''
#define main radio_main
#include "SOURCE"
#undef main
#include <cassert>
int main(int argc,char** argv){
 assert(argc==2);std::string folder=std::string(argv[1])+"/";directory=folder.c_str();
 auto bus=dbus_bus_get_private(DBUS_BUS_SESSION,nullptr);assert(bus);
 auto snapshot=[&](const char* text){std::ofstream file(folder+"device.json");file<<text;file.close();questPower::synchronize(bus,true);};
 auto percentage=[] {return std::get<double>(questPower::objects.at(questPower::display).at("Percentage"));};
 auto state=[] {return std::get<uint32_t>(questPower::objects.at(questPower::display).at("State"));};
 auto onBattery=[] {return std::get<bool>(questPower::objects.at(questPower::root).at("OnBattery"));};
 snapshot(R"({"battery_percent":71,"battery_status":2,"battery_plugged":2,"battery_voltage_mv":4120,"battery_temperature_tenths_c":325,"battery_charge_time_ms":600000})");
 assert(percentage()==71&&state()==1&&!onBattery());
 auto& props=questPower::objects.at(questPower::display);
 assert(std::get<double>(props.at("Voltage"))==4.12);
 assert(std::get<double>(props.at("Temperature"))==32.5);
 assert(std::get<int64_t>(props.at("TimeToFull"))==600);
 auto call=[&](const std::string& path,const char* iface,const char* method){auto m=dbus_message_new_method_call(questPower::service,path.c_str(),iface,method);dbus_message_set_serial(m,42);return m;};
 auto request=call(questPower::root,questPower::service,"GetDisplayDevice");assert(questPower::owns(request));
 auto reply=questPower::handle(bus,request);assert(dbus_message_has_signature(reply,"o"));
 const char* path=nullptr;assert(dbus_message_get_args(reply,nullptr,DBUS_TYPE_OBJECT_PATH,&path,DBUS_TYPE_INVALID));assert(path==questPower::display);dbus_message_unref(request);dbus_message_unref(reply);
 request=call(questPower::root,questPower::service,"EnumerateDevices");reply=questPower::handle(bus,request);assert(dbus_message_has_signature(reply,"ao"));dbus_message_unref(request);dbus_message_unref(reply);
 request=call(questPower::display,DBUS_INTERFACE_PROPERTIES,"GetAll");const char* iface=questPower::deviceInterface;dbus_message_append_args(request,DBUS_TYPE_STRING,&iface,DBUS_TYPE_INVALID);reply=questPower::handle(bus,request);assert(dbus_message_has_signature(reply,"a{sv}"));
 DBusMessageIter iter,array,entry,value;dbus_message_iter_init(reply,&iter);dbus_message_iter_recurse(&iter,&array);bool checked=false;
 while(dbus_message_iter_get_arg_type(&array)==DBUS_TYPE_DICT_ENTRY){dbus_message_iter_recurse(&array,&entry);const char* key;dbus_message_iter_get_basic(&entry,&key);dbus_message_iter_next(&entry);dbus_message_iter_recurse(&entry,&value);if(!strcmp(key,"Percentage")){assert(dbus_message_iter_get_arg_type(&value)==DBUS_TYPE_DOUBLE);double p;dbus_message_iter_get_basic(&value,&p);assert(p==71);checked=true;}dbus_message_iter_next(&array);}assert(checked);dbus_message_unref(reply);dbus_message_unref(request);
 // Unplugging must update both root AC state and device charging state.
 snapshot(R"({"battery_percent":70,"battery_status":3,"battery_plugged":0,"battery_charge_time_ms":-1})");
 assert(percentage()==70&&state()==2&&onBattery());assert(std::get<int64_t>(questPower::objects.at(questPower::display).at("TimeToFull"))==0);
 snapshot(R"({"battery_percent":100,"battery_status":5,"battery_plugged":1})");assert(state()==4&&!onBattery());
 snapshot(R"({"battery_percent":70,"battery_status":4,"battery_plugged":1})");assert(state()==5&&!onBattery()); // Connected, but charging paused by Android.
 snapshot(R"({"battery_percent":4,"battery_status":3,"battery_plugged":0})");assert(std::get<uint32_t>(questPower::objects.at(questPower::display).at("WarningLevel"))==4);
 snapshot(R"({"battery_percent":-1})");assert(!std::get<bool>(questPower::objects.at(questPower::display).at("IsPresent")));assert(state()==0);
 snapshot(R"({"battery_percent":82,"charging":true})");assert(percentage()==82&&state()==1&&!onBattery());
 request=call(questPower::display,DBUS_INTERFACE_PROPERTIES,"Get");iface=questPower::deviceInterface;const char* key="Nonexistent";dbus_message_append_args(request,DBUS_TYPE_STRING,&iface,DBUS_TYPE_STRING,&key,DBUS_TYPE_INVALID);reply=questPower::handle(bus,request);assert(dbus_message_is_error(reply,DBUS_ERROR_UNKNOWN_PROPERTY));dbus_message_unref(request);dbus_message_unref(reply);
 dbus_connection_close(bus);dbus_connection_unref(bus);
}
'''


class PowerBridgeTest(unittest.TestCase):
    def test_upower_properties_and_state_transitions(self):
        source = Path(__file__).resolve().parents[1] / 'linux/bluetooth-bridge.cpp'
        with tempfile.TemporaryDirectory() as folder:
            code, binary = Path(folder) / 'power.cpp', Path(folder) / 'power-test'
            code.write_text(HARNESS.replace('SOURCE', str(source)))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'dbus-1'], text=True).split()
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(code), *flags, '-o', str(binary)], check=True)
            subprocess.run(['dbus-run-session', '--', str(binary), folder], check=True)


if __name__ == '__main__':
    unittest.main()
