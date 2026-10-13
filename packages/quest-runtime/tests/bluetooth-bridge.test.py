"""Exercise real D-Bus messages against Android radio snapshots and replies."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'linux/bluetooth-bridge.cpp'
HARNESS = r'''
#define main radio_main
#include "SOURCE"
#undef main
#include <cassert>
#include <thread>

static DBusMessage *request(const char *iface, const char *method, const char *path = "/org/bluez/hci0") {
  auto value = dbus_message_new_method_call("org.bluez", path, iface, method);
  dbus_message_set_serial(value, 42);
  return value;
}
static void snapshot(const char *contents) {
  std::ofstream file(std::string(directory) + "bluetooth-devices.tsv"); file << contents;
}
int main(int argc, char **argv) {
  assert(argc == 2); std::string root = std::string(argv[1]) + "/"; directory = root.c_str();
  auto bus = dbus_bus_get_private(DBUS_BUS_SESSION, nullptr); assert(bus);
  snapshot("adapter\t1\nAA:BB:CC:DD:EE:FF\tController\t10\t0\t1288\t-50\t\ninvalid/path\tBad\t12\t1\t0\t0\n");
  synchronize(bus); assert(discovering && devices.size() == 1);
  const char *path = "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF";
  auto get = request("org.freedesktop.DBus.Properties", "Get", path);
  const char *iface = "org.bluez.Device1", *key = "UUIDs";
  dbus_message_append_args(get, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
  auto reply = handle(bus, get); assert(reply && dbus_message_has_signature(reply, "v"));
  DBusMessageIter value, array; dbus_message_iter_init(reply, &value); dbus_message_iter_recurse(&value, &array);
  assert(dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_ARRAY);
  assert(dbus_message_iter_get_element_type(&array) == DBUS_TYPE_STRING);
  dbus_message_unref(reply); dbus_message_unref(get);
  auto pair = request("org.bluez.Device1", "Pair", path);
  dbus_message_set_sender(pair, dbus_bus_get_unique_name(bus));
  std::thread android([&] {
    while (read("bluetooth-command.json").empty()) usleep(1000);
    auto request = read("bluetooth-command.json"); auto start = request.find(":") + 1;
    auto id = request.substr(start, request.find(",", start) - start);
    std::ofstream file(root + "bluetooth-reply.json"); file << "{\"id\":" << id << ",\"success\":true}";
  });
  reply = handle(bus, pair); android.join(); assert(!reply && pending.size() == 1);
  snapshot("adapter\t0\nAA:BB:CC:DD:EE:FF\tController\t11\t0\t1288\t-50\t\n");
  synchronize(bus); assert(pending.size() == 1 && !discovering);
  snapshot("adapter\t0\nAA:BB:CC:DD:EE:FF\tController\t12\t1\t1288\t-50\t00001124-0000-1000-8000-00805f9b34fb\n");
  synchronize(bus); assert(pending.empty()); dbus_message_unref(pair);
  auto connect = request("org.bluez.Device1", "Connect", path);
  pending.push_back({dbus_message_ref(connect), path, "connect", std::chrono::steady_clock::now() + std::chrono::seconds(60)});
  snapshot("adapter\t0\nAA:BB:CC:DD:EE:FF\tController\t12\t0\t1288\t-50\t00001124-0000-1000-8000-00805f9b34fb\n");
  synchronize(bus); assert(pending.size() == 1); // A bond and UUIDs alone do not complete connection.
  snapshot("adapter\t0\nAA:BB:CC:DD:EE:FF\tController\t12\t1\t1288\t-50\t00001124-0000-1000-8000-00805f9b34fb\n");
  synchronize(bus); assert(pending.empty()); dbus_message_unref(connect);
  auto trust = request("org.freedesktop.DBus.Properties", "Set", path);
  key = "Trusted"; dbus_bool_t trusted = true; DBusMessageIter args, variant;
  dbus_message_iter_init_append(trust, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "b", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &trusted);
  dbus_message_iter_close_container(&args, &variant);
  reply = handle(bus, trust); assert(dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
  dbus_message_unref(reply); dbus_message_unref(trust);
  unlink((root + "bluetooth-command.json").c_str());
  auto forget = request("org.bluez.Adapter1", "RemoveDevice");
  dbus_message_set_sender(forget, dbus_bus_get_unique_name(bus));
  dbus_message_append_args(forget, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);
  std::thread androidForget([&] {
    while (read("bluetooth-command.json").empty()) usleep(1000);
    auto command = read("bluetooth-command.json"); assert(command.find("forget") != std::string::npos);
    auto start = command.find(":") + 1;
    auto id = command.substr(start, command.find(",", start) - start);
    std::ofstream file(root + "bluetooth-reply.json"); file << "{\"id\":" << id << ",\"success\":true}";
  });
  reply = handle(bus, forget); androidForget.join(); assert(!reply && pending.size() == 1);
  synchronize(bus); assert(pending.size() == 1);
  snapshot("adapter\t0\nAA:BB:CC:DD:EE:FF\tController\t10\t0\t1288\t-50\t\n");
  synchronize(bus); assert(pending.empty()); dbus_message_unref(forget);
  auto objects = request("org.freedesktop.DBus.ObjectManager", "GetManagedObjects", "/");
  reply = handle(bus, objects); assert(reply && dbus_message_has_signature(reply, "a{oa{sa{sv}}}"));
  dbus_message_unref(reply); dbus_message_unref(objects);
  dbus_connection_close(bus); dbus_connection_unref(bus);
  puts("PASS: real device enumeration, empty UUID variant, trust, and deferred pairing/forget results");
}
'''


class BluetoothBridgeTest(unittest.TestCase):
    def test_discovery_and_pairing_use_android_results(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            harness = root / 'test.cpp'
            harness.write_text(HARNESS.replace('SOURCE', str(SOURCE)))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'dbus-1'], text=True).split()
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(harness), '-pthread', *flags, '-o', str(root / 'test')], check=True)
            bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'], stdout=subprocess.PIPE, text=True)
            try:
                address = bus.stdout.readline().strip()
                subprocess.run([str(root / 'test'), folder], env={**os.environ, 'DBUS_SESSION_BUS_ADDRESS': address}, check=True, timeout=15)
            finally:
                bus.terminate()
                bus.wait(timeout=5)
                bus.stdout.close()


if __name__ == '__main__':
    unittest.main()
