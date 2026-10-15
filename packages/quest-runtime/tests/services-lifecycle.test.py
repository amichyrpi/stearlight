"""The embedded device services release their D-Bus names on shutdown."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'linux/bluetooth-bridge.cpp'
HARNESS = r'''
#define QUEST_SERVICES_EMBEDDED
#include "SOURCE"
#include <cassert>
#include <thread>
int main(int argc, char** argv) {
  assert(argc == 2);
  std::string root = std::string(argv[1]) + "/";
  directory = root.c_str();
  dbus_threads_init_default();
  auto bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, nullptr);
  assert(bus);
  std::thread services([] { assert(quest_services_run() == 0); });
  for (unsigned i = 0; i < 200 && !dbus_bus_name_has_owner(bus, "org.freedesktop.UPower", nullptr); ++i)
    usleep(10000);
  assert(dbus_bus_name_has_owner(bus, "org.bluez", nullptr));
  assert(dbus_bus_name_has_owner(bus, "org.freedesktop.NetworkManager", nullptr));
  assert(dbus_bus_name_has_owner(bus, "org.freedesktop.UPower", nullptr));
  quest_services_stop();
  services.join();
  assert(!dbus_bus_name_has_owner(bus, "org.bluez", nullptr));
  assert(!dbus_bus_name_has_owner(bus, "org.freedesktop.NetworkManager", nullptr));
  assert(!dbus_bus_name_has_owner(bus, "org.freedesktop.UPower", nullptr));
  dbus_connection_close(bus);
  dbus_connection_unref(bus);
}
'''


class ServicesLifecycleTest(unittest.TestCase):
    def test_embedded_services_start_and_stop_cleanly(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'test.cpp'
            binary = Path(folder) / 'test'
            source.write_text(HARNESS.replace('SOURCE', str(SOURCE)))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'dbus-1'], text=True).split()
            subprocess.run(['g++', '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', str(source), *flags, '-o', str(binary)], check=True)
            bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'], stdout=subprocess.PIPE, text=True)
            try:
                address = bus.stdout.readline().strip()
                subprocess.run([str(binary), folder], env={**os.environ, 'DBUS_SYSTEM_BUS_ADDRESS': address}, check=True, timeout=10)
            finally:
                bus.terminate()
                bus.wait(timeout=5)
                bus.stdout.close()


if __name__ == '__main__':
    unittest.main()
