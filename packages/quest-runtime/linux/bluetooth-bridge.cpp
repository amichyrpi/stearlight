// BlueZ LE advertising API backed by the Quest's actual Android radio.
// Pairing and Linux HCI access are deliberately not advertised as supported.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dbus/dbus.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

static const char *directory = "/run/stearlight/quest/";
static bool stopping = false;
static std::string owner, advertisement, agentOwner, agentPath;
static unsigned requestId = 0;
static bool active = false;
static void stop(int) { stopping = true; }
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
static void properties(DBusMessageIter *parent, const char *interface) {
  DBusMessageIter array;
  dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "{sv}", &array);
  if (!strcmp(interface, "org.bluez.Adapter1")) {
    stringProperty(&array, "Name", "Quest 2");
    stringProperty(&array, "Alias", "Steam Frame");
    booleanProperty(&array, "Powered", state("bluetooth_enabled"));
    booleanProperty(&array, "Discoverable", false);
    booleanProperty(&array, "Discovering", false);
    booleanProperty(&array, "Pairable", false);
  } else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
    byteProperty(&array, "ActiveInstances", active ? 1 : 0);
    byteProperty(&array, "SupportedInstances",
                 state("bluetooth_le_advertising") ? 1 : 0);
  }
  dbus_message_iter_close_container(parent, &array);
}
static void interface(DBusMessageIter *parent, const char *name) {
  DBusMessageIter entry;
  dbus_message_iter_open_container(parent, DBUS_TYPE_DICT_ENTRY, nullptr,
                                   &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
  properties(&entry, name);
  dbus_message_iter_close_container(parent, &entry);
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
    dbus_message_iter_close_container(&output, &objects);
  } else if (!strcmp(iface, "org.freedesktop.DBus.Properties") &&
             !strcmp(member, "GetAll")) {
    const char *requested = nullptr;
    if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_STRING, &requested,
                               DBUS_TYPE_INVALID))
      return fail(DBUS_ERROR_INVALID_ARGS, "Expected interface");
    DBusMessageIter output;
    dbus_message_iter_init_append(reply, &output);
    properties(&output, requested);
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
                "The Quest bridge currently supports LE advertising only");
  return reply;
}
int main() {
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  requestId = static_cast<unsigned>(getpid()) * 10000;
  DBusError error;
  dbus_error_init(&error);
  auto bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  if (!bus) {
    std::fprintf(stderr, "Quest Bluetooth system bus: %s\n", error.message);
    dbus_error_free(&error);
    return 1;
  }
  if (dbus_bus_request_name(bus, "org.bluez", DBUS_NAME_FLAG_DO_NOT_QUEUE,
                            &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    return 1;
  dbus_bus_add_match(bus,
                     "type='signal',interface='org.freedesktop.DBus',member='"
                     "NameOwnerChanged'",
                     nullptr);
  std::puts("QUEST_BLUETOOTH_BRIDGE_READY");
  while (!stopping && dbus_connection_read_write(bus, 100)) {
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
      auto reply = handle(bus, request);
      dbus_connection_send(bus, reply, nullptr);
      dbus_message_unref(reply);
    }
    dbus_message_unref(request);
  }
  if (active) {
    std::string error;
    command("stop", "{}", error);
  }
  dbus_connection_unref(bus);
  return 0;
}
