// BlueZ discovery, bonding and advertising backed by Android's actual radio.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <map>
#include <vector>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dbus/dbus.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

static const char *directory = "/run/stearlight/quest/";
static std::atomic_bool stopping{false};
static std::string owner, advertisement, agentOwner, agentPath;
static unsigned requestId = 0;
static bool active = false;
static bool discovering = false;
struct Device {
  std::string address, name, uuids;
  int bond = 10, connected = 0, deviceClass = 0, rssi = -127;
  std::string record;
};
static std::map<std::string, Device> devices;
struct Pending {
  DBusMessage *request;
  std::string path, operation;
  std::chrono::steady_clock::time_point deadline;
};
static std::vector<Pending> pending;
static bool completed(const Pending &operation, const Device &device) {
  if (operation.operation == "pair") return device.bond == 12;
  if (operation.operation == "forget") return device.bond == 10;
  if (operation.operation == "cancel-pair") return device.bond != 11;
  return operation.operation == "connect" ? device.connected != 0 : device.connected == 0;
}
static std::string devicePath(const std::string &address) {
  auto path = address;
  std::replace(path.begin(), path.end(), ':', '_');
  return "/org/bluez/hci0/dev_" + path;
}
static bool validAddress(const std::string &value) {
  if (value.size() != 17) return false;
  for (unsigned i = 0; i < value.size(); i++)
    if (i % 3 == 2 ? value[i] != ':' : !std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
  return true;
}
#ifndef QUEST_SERVICES_EMBEDDED
static void stop(int) { stopping = true; }
#endif
static std::string read(const char *name) {
  std::ifstream file(std::string(directory) + name);
  return std::string(std::istreambuf_iterator<char>(file), {});
}
static bool state(const char *key) {
  auto data = read("device.json");
  return data.find(std::string("\"") + key + "\":true") != std::string::npos;
}
static std::string quote(const std::string &value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += c;
    } else if (c < 32) {
      char escaped[7];
      snprintf(escaped, sizeof escaped, "\\u%04x", c);
      result += escaped;
    } else
      result += c;
  }
  return result + '"';
}
static std::string json(DBusMessageIter iterator) {
  int type = dbus_message_iter_get_arg_type(&iterator);
  if (type == DBUS_TYPE_VARIANT) {
    DBusMessageIter child;
    dbus_message_iter_recurse(&iterator, &child);
    return json(child);
  }
  if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH) {
    const char *value;
    dbus_message_iter_get_basic(&iterator, &value);
    return quote(value);
  }
  if (type == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t value;
    dbus_message_iter_get_basic(&iterator, &value);
    return value ? "true" : "false";
  }
  if (type == DBUS_TYPE_BYTE) {
    unsigned char value;
    dbus_message_iter_get_basic(&iterator, &value);
    return std::to_string(value);
  }
  if (type == DBUS_TYPE_UINT16) {
    dbus_uint16_t value;
    dbus_message_iter_get_basic(&iterator, &value);
    return std::to_string(value);
  }
  if (type == DBUS_TYPE_UINT32) {
    dbus_uint32_t value;
    dbus_message_iter_get_basic(&iterator, &value);
    return std::to_string(value);
  }
  if (type == DBUS_TYPE_ARRAY) {
    DBusMessageIter child;
    dbus_message_iter_recurse(&iterator, &child);
    bool dictionary =
        dbus_message_iter_get_element_type(&iterator) == DBUS_TYPE_DICT_ENTRY;
    std::string result = dictionary ? "{" : "[";
    bool first = true;
    while (dbus_message_iter_get_arg_type(&child) != DBUS_TYPE_INVALID) {
      if (!first)
        result += ',';
      first = false;
      if (dictionary) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&child, &entry);
        int keyType = dbus_message_iter_get_arg_type(&entry);
        std::string key = json(entry);
        if (keyType != DBUS_TYPE_STRING && keyType != DBUS_TYPE_OBJECT_PATH)
          key = quote(key);
        dbus_message_iter_next(&entry);
        result += key + ":" + json(entry);
      } else
        result += json(child);
      dbus_message_iter_next(&child);
    }
    return result + (dictionary ? "}" : "]");
  }
  return "null";
}
static bool command(const char *operation, const std::string &properties,
                    std::string &error) {
  unsigned id = ++requestId;
  unlink((std::string(directory) + "bluetooth-reply.json").c_str());
  std::ofstream output(std::string(directory) + "bluetooth-command.tmp");
  output << "{\"id\":" << id << ",\"operation\":" << quote(operation)
         << ",\"properties\":" << properties << "}";
  output.close();
  if (rename((std::string(directory) + "bluetooth-command.tmp").c_str(),
             (std::string(directory) + "bluetooth-command.json").c_str())) {
    error = "Cannot publish radio request";
    return false;
  }
  for (int i = 0; i < 500 && !stopping; i++) {
    auto result = read("bluetooth-reply.json");
    if (result.find("\"id\":" + std::to_string(id) + ",") !=
        std::string::npos) {
      if (result.find("\"success\":true") != std::string::npos)
        return true;
      error = "Android Bluetooth request failed (see radio log)";
      return false;
    }
    usleep(10000);
  }
  error = "Android Bluetooth response timed out";
  return false;
}
static void stringProperty(DBusMessageIter *array, const char *key,
                           const char *value) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void booleanProperty(DBusMessageIter *array, const char *key,
                            bool value) {
  DBusMessageIter entry, variant;
  dbus_bool_t data = value;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &data);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void byteProperty(DBusMessageIter *array, const char *key,
                         unsigned char value) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "y", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BYTE, &value);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void numberProperty(DBusMessageIter *array, const char *key, int type, int value) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, type == DBUS_TYPE_INT16 ? "n" : "u", &variant);
  dbus_int16_t shortValue = value;
  dbus_uint32_t longValue = value;
  dbus_message_iter_append_basic(&variant, type, type == DBUS_TYPE_INT16 ? static_cast<void *>(&shortValue) : &longValue);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void objectProperty(DBusMessageIter *array, const char *key, const char *value) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &value);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void uuidProperty(DBusMessageIter *array, const char *key, const std::string &values) {
  DBusMessageIter entry, variant, strings;
  dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &variant);
  dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &strings);
  std::istringstream input(values); std::string uuid;
  while (std::getline(input, uuid, ',')) if (!uuid.empty()) {
    const char *value = uuid.c_str(); dbus_message_iter_append_basic(&strings, DBUS_TYPE_STRING, &value);
  }
  dbus_message_iter_close_container(&variant, &strings);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(array, &entry);
}
static void properties(DBusMessageIter *parent, const char *interface, const char *path = "/org/bluez/hci0") {
  DBusMessageIter array;
  dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "{sv}", &array);
  if (!strcmp(interface, "org.bluez.Adapter1")) {
    stringProperty(&array, "Name", "Quest 2");
    stringProperty(&array, "Alias", "Steam Frame");
    booleanProperty(&array, "Powered", state("bluetooth_enabled"));
    booleanProperty(&array, "Discoverable", false);
    booleanProperty(&array, "Discovering", discovering);
    booleanProperty(&array, "Pairable", state("bluetooth_enabled"));
  } else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
    byteProperty(&array, "ActiveInstances", active ? 1 : 0);
    byteProperty(&array, "SupportedInstances",
                 state("bluetooth_le_advertising") ? 1 : 0);
  } else if (!strcmp(interface, "org.bluez.Device1") && devices.count(path)) {
    const auto &device = devices.at(path);
    stringProperty(&array, "Address", device.address.c_str());
    stringProperty(&array, "Name", device.name.c_str());
    stringProperty(&array, "Alias", device.name.c_str());
    int major = device.deviceClass & 0x1f00;
    const char *icon = major == 0x500 ? "input-gaming" : major == 0x400 ? "audio-card" : major == 0x200 ? "phone" : "bluetooth";
    stringProperty(&array, "Icon", icon);
    booleanProperty(&array, "Paired", device.bond == 12);
    booleanProperty(&array, "Bonded", device.bond == 12);
    booleanProperty(&array, "Trusted", device.bond == 12);
    booleanProperty(&array, "Connected", device.connected != 0);
    booleanProperty(&array, "Blocked", false);
    booleanProperty(&array, "ServicesResolved", device.connected != 0 && !device.uuids.empty());
    numberProperty(&array, "Class", DBUS_TYPE_UINT32, device.deviceClass);
    if (device.rssi != -127) numberProperty(&array, "RSSI", DBUS_TYPE_INT16, device.rssi);
    objectProperty(&array, "Adapter", "/org/bluez/hci0");
    uuidProperty(&array, "UUIDs", device.uuids);
  }
  dbus_message_iter_close_container(parent, &array);
}
static void interface(DBusMessageIter *parent, const char *name, const char *path = "/org/bluez/hci0") {
  DBusMessageIter entry;
  dbus_message_iter_open_container(parent, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  properties(&entry, name, path);
  dbus_message_iter_close_container(parent, &entry);
}
static void changed(DBusConnection *bus, const char *path, const char *name) {
  auto signal = dbus_message_new_signal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
  DBusMessageIter output, invalidated; dbus_message_iter_init_append(signal, &output);
  dbus_message_iter_append_basic(&output, DBUS_TYPE_STRING, &name);
  properties(&output, name, path);
  dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "s", &invalidated);
  dbus_message_iter_close_container(&output, &invalidated);
  dbus_connection_send(bus, signal, nullptr); dbus_message_unref(signal);
}
static void synchronize(DBusConnection *bus) {
  std::istringstream input(read("bluetooth-devices.tsv")); std::string line;
  std::map<std::string, Device> updated;
  while (std::getline(input, line)) {
    if (line.rfind("adapter\t", 0) == 0) {
      bool next = line == "adapter\t1";
      if (discovering != next) { discovering = next; changed(bus, "/org/bluez/hci0", "org.bluez.Adapter1"); }
      continue;
    }
    std::istringstream row(line); std::vector<std::string> fields; std::string field;
    while (std::getline(row, field, '\t')) fields.push_back(field);
    if (fields.size() < 6 || !validAddress(fields[0])) continue;
    Device device; device.address = fields[0]; device.name = fields[1]; device.record = line;
    try { device.bond = std::stoi(fields[2]); device.connected = std::stoi(fields[3]); device.deviceClass = std::stoi(fields[4]); device.rssi = std::stoi(fields[5]); } catch (...) { continue; }
    if (fields.size() > 6) device.uuids = fields[6];
    updated[devicePath(device.address)] = device;
  }
  auto previous = std::move(devices); devices = std::move(updated);
  for (const auto &[path, device] : devices) {
    auto old = previous.find(path);
    if (old == previous.end()) {
      auto signal = dbus_message_new_signal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
      DBusMessageIter output, interfaces; dbus_message_iter_init_append(signal, &output); const char *object = path.c_str();
      dbus_message_iter_append_basic(&output, DBUS_TYPE_OBJECT_PATH, &object);
      dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "{sa{sv}}", &interfaces);
      interface(&interfaces, "org.bluez.Device1", object);
      dbus_message_iter_close_container(&output, &interfaces);
      dbus_connection_send(bus, signal, nullptr); dbus_message_unref(signal);
    } else if (old->second.record != device.record) changed(bus, path.c_str(), "org.bluez.Device1");
  }
  for (const auto &[path, device] : previous) if (!devices.count(path)) {
    auto signal = dbus_message_new_signal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
    DBusMessageIter output, names; dbus_message_iter_init_append(signal, &output); const char *object = path.c_str(), *name = "org.bluez.Device1";
    dbus_message_iter_append_basic(&output, DBUS_TYPE_OBJECT_PATH, &object);
    dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "s", &names);
    dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &name);
    dbus_message_iter_close_container(&output, &names);
    dbus_connection_send(bus, signal, nullptr); dbus_message_unref(signal);
  }
  for (auto it = pending.begin(); it != pending.end();) {
    auto device = devices.find(it->path);
    bool complete = device != devices.end() && completed(*it, device->second);
    if (!complete && std::chrono::steady_clock::now() < it->deadline) { ++it; continue; }
    auto reply = complete ? dbus_message_new_method_return(it->request) : dbus_message_new_error(it->request, "org.bluez.Error.Failed", "Android did not complete the device operation");
    dbus_connection_send(bus, reply, nullptr); dbus_message_unref(reply); dbus_message_unref(it->request); it = pending.erase(it);
  }
}
static void copyValue(DBusMessageIter *output, DBusMessageIter source) {
  int type = dbus_message_iter_get_arg_type(&source);
  if (dbus_type_is_basic(type)) { DBusBasicValue value{}; dbus_message_iter_get_basic(&source, &value); dbus_message_iter_append_basic(output, type, &value); return; }
  DBusMessageIter child, result; dbus_message_iter_recurse(&source, &child);
  char *signature = dbus_message_iter_get_signature(type == DBUS_TYPE_ARRAY ? &source : &child);
  dbus_message_iter_open_container(output, type, type == DBUS_TYPE_ARRAY ? signature + 1 : type == DBUS_TYPE_VARIANT ? signature : nullptr, &result); dbus_free(signature);
  while (dbus_message_iter_get_arg_type(&child) != DBUS_TYPE_INVALID) { copyValue(&result, child); dbus_message_iter_next(&child); }
  dbus_message_iter_close_container(output, &result);
}
static DBusMessage *handle(DBusConnection *bus, DBusMessage *request) {
  const char *member = dbus_message_get_member(request);
  const char *path = dbus_message_get_path(request);
  const char *iface = dbus_message_get_interface(request);
  const char *sender = dbus_message_get_sender(request);
  auto reply = dbus_message_new_method_return(request);
  auto fail = [&](const char *name, const char *text) {
    dbus_message_unref(reply);
    return dbus_message_new_error(request, name, text);
  };
  if (!member || !iface || !path)
    return fail(DBUS_ERROR_UNKNOWN_METHOD, "Missing interface");
  if (!strcmp(iface, "org.freedesktop.DBus.ObjectManager") &&
      !strcmp(member, "GetManagedObjects")) {
    DBusMessageIter output, objects, object, interfaces;
    dbus_message_iter_init_append(reply, &output);
    dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "{oa{sa{sv}}}",
                                     &objects);
    const char *adapter = "/org/bluez/hci0";
    dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, nullptr,
                                     &object);
    dbus_message_iter_append_basic(&object, DBUS_TYPE_OBJECT_PATH, &adapter);
    dbus_message_iter_open_container(&object, DBUS_TYPE_ARRAY, "{sa{sv}}",
                                     &interfaces);
    interface(&interfaces, "org.bluez.Adapter1");
    interface(&interfaces, "org.bluez.LEAdvertisingManager1");
    dbus_message_iter_close_container(&object, &interfaces);
    dbus_message_iter_close_container(&objects, &object);
    for (const auto &[path, device] : devices) {
      const char *deviceObject = path.c_str();
      dbus_message_iter_open_container(&objects, DBUS_TYPE_DICT_ENTRY, nullptr, &object);
      dbus_message_iter_append_basic(&object, DBUS_TYPE_OBJECT_PATH, &deviceObject);
      dbus_message_iter_open_container(&object, DBUS_TYPE_ARRAY, "{sa{sv}}", &interfaces);
      interface(&interfaces, "org.bluez.Device1", deviceObject);
      dbus_message_iter_close_container(&object, &interfaces); dbus_message_iter_close_container(&objects, &object);
    }
    dbus_message_iter_close_container(&output, &objects);
  } else if (!strcmp(iface, "org.freedesktop.DBus.Properties") &&
             !strcmp(member, "GetAll")) {
    const char *requested = nullptr;
    if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &requested,
                               DBUS_TYPE_INVALID))
      return fail(DBUS_ERROR_INVALID_ARGS, "Expected interface");
    DBusMessageIter output;
    dbus_message_iter_init_append(reply, &output);
    properties(&output, requested, path);
  } else if (!strcmp(iface, "org.freedesktop.DBus.Properties") && !strcmp(member, "Get")) {
    const char *requested, *key;
    if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &requested, DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID)) return fail(DBUS_ERROR_INVALID_ARGS, "Expected interface and property");
    auto values = dbus_message_new_method_return(request); DBusMessageIter output, array;
    dbus_message_iter_init_append(values, &output); properties(&output, requested, path);
    dbus_message_iter_init(values, &output); dbus_message_iter_recurse(&output, &array);
    bool found = false;
    while (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_INVALID) {
      DBusMessageIter entry; dbus_message_iter_recurse(&array, &entry); const char *name; dbus_message_iter_get_basic(&entry, &name); dbus_message_iter_next(&entry);
      if (!strcmp(name, key)) { dbus_message_iter_init_append(reply, &output); copyValue(&output, entry); found = true; break; }
      dbus_message_iter_next(&array);
    }
    dbus_message_unref(values); if (!found) return fail(DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown Bluetooth property");
  } else if (!strcmp(iface, "org.freedesktop.DBus.Properties") && !strcmp(member, "Set")) {
    const char *requested, *key; DBusMessageIter args, value;
    if (!dbus_message_iter_init(request, &args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return fail(DBUS_ERROR_INVALID_ARGS, "Expected interface");
    dbus_message_iter_get_basic(&args, &requested);
    if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) return fail(DBUS_ERROR_INVALID_ARGS, "Expected property");
    dbus_message_iter_get_basic(&args, &key);
    if (!dbus_message_iter_next(&args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) return fail(DBUS_ERROR_INVALID_ARGS, "Expected value");
    dbus_message_iter_recurse(&args, &value);
    if (strcmp(requested, "org.bluez.Device1") || strcmp(key, "Trusted") || !devices.count(path)) return fail("org.bluez.Error.NotSupported", "Android owns this Bluetooth property");
    if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_BOOLEAN) return fail(DBUS_ERROR_INVALID_ARGS, "Trusted must be a boolean");
    dbus_bool_t trusted; dbus_message_iter_get_basic(&value, &trusted);
    // Android authorizes its bonded profiles; do not invent a separate trust state.
    if (static_cast<bool>(trusted) != (devices.at(path).bond == 12)) return fail("org.bluez.Error.NotSupported", "Pair or forget the Android bond to change trust");
  } else if (!strcmp(iface, "org.bluez.Adapter1") && !strcmp(member, "RemoveDevice")) {
    const char *object = nullptr;
    if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_OBJECT_PATH, &object, DBUS_TYPE_INVALID)) return fail(DBUS_ERROR_INVALID_ARGS, "Expected device path");
    if (!devices.count(object)) return fail("org.bluez.Error.DoesNotExist", "Unknown Bluetooth device");
    std::string error;
    if (!command("forget", "{\"address\":" + quote(devices.at(object).address) + "}", error)) return fail("org.bluez.Error.Failed", error.c_str());
    pending.push_back({dbus_message_ref(request), object, "forget", std::chrono::steady_clock::now() + std::chrono::seconds(60)});
    dbus_message_unref(reply); return nullptr;
  } else if (!strcmp(iface, "org.bluez.Adapter1") && (!strcmp(member, "StartDiscovery") || !strcmp(member, "StopDiscovery"))) {
    std::string error;
    if (!command(!strcmp(member, "StartDiscovery") ? "discovery-start" : "discovery-stop", "{}", error)) return fail("org.bluez.Error.Failed", error.c_str());
    synchronize(bus);
  } else if (!strcmp(iface, "org.bluez.Adapter1") && !strcmp(member, "SetDiscoveryFilter")) {
    // Android discovers Classic and BLE devices together; no synthetic filters.
  } else if (!strcmp(iface, "org.bluez.Adapter1") && !strcmp(member, "GetDiscoveryFilters")) {
    DBusMessageIter output, values; dbus_message_iter_init_append(reply, &output);
    dbus_message_iter_open_container(&output, DBUS_TYPE_ARRAY, "s", &values);
    dbus_message_iter_close_container(&output, &values);
  } else if (!strcmp(iface, "org.bluez.Device1")) {
    if (!devices.count(path)) return fail("org.bluez.Error.DoesNotExist", "Unknown Bluetooth device");
    std::string operation = !strcmp(member, "Pair") ? "pair" : !strcmp(member, "Connect") ? "connect" : !strcmp(member, "Disconnect") ? "disconnect" : !strcmp(member, "CancelPairing") ? "cancel-pair" : "";
    if (operation.empty()) return fail(DBUS_ERROR_UNKNOWN_METHOD, "Unsupported device operation");
    std::string error;
    if (!command(operation.c_str(), "{\"address\":" + quote(devices.at(path).address) + "}", error)) return fail("org.bluez.Error.Failed", error.c_str());
    pending.push_back({dbus_message_ref(request), path, operation, std::chrono::steady_clock::now() + std::chrono::seconds(60)});
    dbus_message_unref(reply); return nullptr;
  } else if (!strcmp(iface, "org.bluez.AgentManager1")) {
    const char *registered = nullptr;
    DBusMessageIter args;
    if (!dbus_message_iter_init(request, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
      return fail(DBUS_ERROR_INVALID_ARGS, "Expected agent path");
    dbus_message_iter_get_basic(&args, &registered);
    if (!strcmp(member, "RegisterAgent")) {
      agentOwner = sender;
      agentPath = registered;
    } else if (!strcmp(member, "RequestDefaultAgent")) {
      if (agentOwner != sender || agentPath != registered)
        return fail("org.bluez.Error.DoesNotExist", "Agent not registered");
    } else if (!strcmp(member, "UnregisterAgent")) {
      if (agentOwner != sender || agentPath != registered)
        return fail("org.bluez.Error.DoesNotExist", "Agent not registered");
      agentOwner.clear();
      agentPath.clear();
    } else
      return fail(DBUS_ERROR_UNKNOWN_METHOD, "Unsupported agent operation");
  } else if (!strcmp(iface, "org.bluez.LEAdvertisingManager1") &&
             !strcmp(member, "RegisterAdvertisement")) {
    if (active)
      return fail("org.bluez.Error.AlreadyExists",
                  "One radio advertisement is already active");
    const char *object = nullptr;
    DBusMessageIter args;
    if (!dbus_message_iter_init(request, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH)
      return fail(DBUS_ERROR_INVALID_ARGS, "Expected advertisement path");
    dbus_message_iter_get_basic(&args, &object);
    auto get = dbus_message_new_method_call(
        sender, object, "org.freedesktop.DBus.Properties", "GetAll");
    const char *advertisementInterface = "org.bluez.LEAdvertisement1";
    dbus_message_append_args(get, DBUS_TYPE_STRING, &advertisementInterface,
                             DBUS_TYPE_INVALID);
    DBusError error;
    dbus_error_init(&error);
    auto values =
        dbus_connection_send_with_reply_and_block(bus, get, 3000, &error);
    dbus_message_unref(get);
    if (!values) {
      dbus_error_free(&error);
      return fail("org.bluez.Error.Failed",
                  "Cannot read advertisement properties");
    }
    DBusMessageIter propertiesIter;
    dbus_message_iter_init(values, &propertiesIter);
    auto data = json(propertiesIter);
    dbus_message_unref(values);
    std::string failure;
    if (!command("start", data, failure))
      return fail("org.bluez.Error.Failed", failure.c_str());
    owner = sender;
    advertisement = object;
    active = true;
    std::puts("QUEST_BLUETOOTH_ADVERTISING_STARTED");
  } else if (!strcmp(iface, "org.bluez.LEAdvertisingManager1") &&
             !strcmp(member, "UnregisterAdvertisement")) {
    const char *object = nullptr;
    if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_OBJECT_PATH, &object,
                               DBUS_TYPE_INVALID))
      return fail(DBUS_ERROR_INVALID_ARGS, "Expected advertisement path");
    if (!active || owner != sender || advertisement != object)
      return fail("org.bluez.Error.DoesNotExist",
                  "Advertisement not registered");
    std::string error;
    if (!command("stop", "{}", error))
      return fail("org.bluez.Error.Failed", error.c_str());
    active = false;
    owner.clear();
    advertisement.clear();
  } else
    return fail("org.bluez.Error.NotSupported",
                "This Bluetooth operation is not supported by Android");
  return reply;
}
#include "network-bridge.h"
extern "C" void quest_services_stop() { stopping = true; }
extern "C" int quest_services_run() {
#ifndef QUEST_SERVICES_EMBEDDED
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
  dbus_threads_init_default();
  requestId = static_cast<unsigned>(getpid()) * 10000;
  DBusError error;
  dbus_error_init(&error);
  auto bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
  if (!bus) {
    std::fprintf(stderr, "Quest Bluetooth system bus: %s\n", error.message);
    dbus_error_free(&error);
    return 1;
  }
  dbus_connection_set_exit_on_disconnect(bus, false);
  for (const char* name : {"org.bluez", "org.freedesktop.NetworkManager"}) {
    if (dbus_bus_request_name(bus, name, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                              &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
      std::fprintf(stderr, "Quest services cannot own %s: %s\n", name,
                   error.message ? error.message : "name already owned");
      dbus_error_free(&error);
      dbus_connection_close(bus);
      dbus_connection_unref(bus);
      return 1;
    }
  }
  dbus_bus_add_match(bus,
                     "type='signal',interface='org.freedesktop.DBus',member='"
                     "NameOwnerChanged'",
                     nullptr);
  std::puts("QUEST_BLUETOOTH_BRIDGE_READY");
  while (!stopping && dbus_connection_read_write(bus, 100)) {
    synchronize(bus);
    questNetwork::synchronize(bus);
    auto request = dbus_connection_pop_message(bus);
    if (!request)
      continue;
    if (dbus_message_is_signal(request, "org.freedesktop.DBus",
                               "NameOwnerChanged")) {
      const char *name, *previous, *next;
      if (dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &name,
                                DBUS_TYPE_STRING, &previous, DBUS_TYPE_STRING,
                                &next, DBUS_TYPE_INVALID) &&
          owner == name && !*next) {
        std::string error;
        command("stop", "{}", error);
        active = false;
        owner.clear();
        advertisement.clear();
      }
    } else if (dbus_message_get_type(request) ==
               DBUS_MESSAGE_TYPE_METHOD_CALL) {
      auto reply = questNetwork::owns(request) ? questNetwork::handle(request) : handle(bus, request);
      if (reply) { dbus_connection_send(bus, reply, nullptr); dbus_message_unref(reply); }
    }
    dbus_message_unref(request);
  }
  for (auto &operation : pending) dbus_message_unref(operation.request);
  if (active) {
    std::string error;
    command("stop", "{}", error);
  }
  dbus_connection_close(bus);
  dbus_connection_unref(bus);
  return 0;
}
#ifndef QUEST_SERVICES_EMBEDDED
int main() { return quest_services_run(); }
#endif
