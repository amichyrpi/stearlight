"""Check D-Bus Wi-Fi observations and delegated controls without a physical radio."""
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
 auto snapshot=[&](const char* text){std::ofstream file(folder+"network.tsv");file<<text;file.close();questNetwork::synchronize(bus);};
 snapshot("enabled\t1\nconnected\t1\nvalidated\t1\ncurrent\tVGVzdA==\tQUE6QkI6Q0M6REQ6RUU6RkY=\nap\tVGVzdA==\tQUE6QkI6Q0M6REQ6RUU6RkY=\t75\t5180\t392\n");
 const auto manager=questNetwork::objects.at(questNetwork::root).at(questNetwork::manager);
 assert(manager.at("State").number==70&&manager.at("WirelessEnabled").number==1);
 assert(questNetwork::objects.at(questNetwork::root+"/Settings").at("org.freedesktop.NetworkManager.Settings").at("Hostname").text=="frame");
 const auto wifi=questNetwork::objects.at(questNetwork::device).at("org.freedesktop.NetworkManager.Device.Wireless");
 auto ap=wifi.at("ActiveAccessPoint").text;assert(ap!="/");
 const auto props=questNetwork::objects.at(ap).at("org.freedesktop.NetworkManager.AccessPoint");
 assert(props.at("Ssid").text=="Test"&&props.at("Strength").number==75&&props.at("RsnFlags").number==392);
 const auto state=questNetwork::objects.at(questNetwork::device).at("org.freedesktop.NetworkManager.Device").at("StateReason");
 assert(state.signature=="(uu)"&&state.number==100); // libnm uses StateReason, not State.
 auto message=dbus_message_new_method_call(questNetwork::manager,"/org/freedesktop","org.freedesktop.DBus.ObjectManager","GetManagedObjects");dbus_message_set_serial(message,42);
 assert(questNetwork::owns(message));auto reply=questNetwork::handle(message);assert(dbus_message_has_signature(reply,"a{oa{sa{sv}}}"));dbus_message_unref(reply);dbus_message_unref(message);
 snapshot("enabled\t1\nconnected\t0\nvalidated\t0\ncurrent\t\t\nap\tVGVzdA==\tQUE6QkI6Q0M6REQ6RUU6RkY=\t20\t5180\t392\n");
 assert(!questNetwork::objects.count(questNetwork::active));
 assert(questNetwork::objects.at(questNetwork::device).at("org.freedesktop.NetworkManager.Device.Wireless").at("ActiveAccessPoint").text=="/");
 message=dbus_message_new_method_call(questNetwork::manager,questNetwork::root.c_str(),questNetwork::manager,"DeactivateConnection");dbus_message_set_serial(message,43);
 reply=questNetwork::handle(message);assert(dbus_message_get_type(reply)==DBUS_MESSAGE_TYPE_ERROR);
 questNetwork::command("scan");
 assert(read("network-settings.request")=="settings\n"&&read("network-scan.request")=="scan\n"); // A background scan cannot overwrite a user action.
 assert(questNetwork::objects.at(questNetwork::root).at(questNetwork::manager).at("WirelessEnabled").number==1);
 dbus_message_unref(reply);dbus_message_unref(message);
 snapshot("enabled\t0\nconnected\t0\nvalidated\t0\ncurrent\t\t\n");
 assert(!questNetwork::objects.count(ap));
 assert(questNetwork::objects.at(questNetwork::root).at(questNetwork::manager).at("WirelessEnabled").number==0);
 dbus_connection_close(bus);dbus_connection_unref(bus);
}
'''

class NetworkBridgeTest(unittest.TestCase):
    def test_real_state_messages_and_control_delegation(self):
        source = Path(__file__).resolve().parents[1] / 'linux/bluetooth-bridge.cpp'
        with tempfile.TemporaryDirectory() as folder:
            code = Path(folder) / 'network.cpp'
            code.write_text(HARNESS.replace('SOURCE', str(source)))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'dbus-1'], text=True).split()
            binary = Path(folder) / 'network-test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(code), *flags, '-o', str(binary)], check=True)
            subprocess.run(['dbus-run-session', '--', str(binary), folder], check=True)

if __name__ == '__main__':
    unittest.main()
