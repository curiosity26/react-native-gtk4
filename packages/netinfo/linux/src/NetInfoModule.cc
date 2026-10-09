#include "NetInfoModule.h"

#include <gio/gio.h>
#include <folly/json.h>
#include <jsi/JSIDynamic.h>

#include <atomic>
#include <mutex>
#include <thread>

using namespace facebook::react;
using facebook::jsi::Runtime;

namespace rngtk_netinfo {

std::string netInfoType(const std::string &nmType) {
  if (nmType == "802-11-wireless" || nmType == "wifi-p2p") return "wifi";
  if (nmType == "802-3-ethernet" || nmType == "veth" || nmType == "bridge" || nmType == "bond") {
    return "ethernet";
  }
  if (nmType == "gsm" || nmType == "cdma") return "cellular";
  if (nmType == "bluetooth") return "bluetooth";
  if (nmType == "wimax") return "wimax";
  if (nmType == "vpn" || nmType == "wireguard" || nmType == "tun") return "vpn";
  if (nmType.empty()) return "other";
  return "other";
}

folly::dynamic netInfoState(bool available, int connectivity, bool metered, const std::string &type,
                            const std::string &ssid, const std::string &ipAddress,
                            const std::string &subnet) {
  // G_NETWORK_CONNECTIVITY_LOCAL (1) means no route out at all: "none".
  bool connected = available && connectivity > G_NETWORK_CONNECTIVITY_LOCAL;
  folly::dynamic state = folly::dynamic::object;
  if (!connected) {
    state["type"] = "none";
    state["isConnected"] = false;
    state["isInternetReachable"] = false;
    state["details"] = nullptr;
    return state;
  }
  state["type"] = type;
  state["isConnected"] = true;
  // PORTAL and LIMITED: connected, but not to the internet (yet).
  state["isInternetReachable"] = connectivity == G_NETWORK_CONNECTIVITY_FULL;
  folly::dynamic details = folly::dynamic::object("isConnectionExpensive", metered);
  auto orNull = [](const std::string &s) { return s.empty() ? folly::dynamic(nullptr) : folly::dynamic(s); };
  if (type == "wifi") {
    details["ssid"] = orNull(ssid);
    details["bssid"] = nullptr;
    details["strength"] = nullptr;
    details["frequency"] = nullptr;
    details["linkSpeed"] = nullptr;
    details["rxLinkSpeed"] = nullptr;
    details["txLinkSpeed"] = nullptr;
  }
  if (type == "cellular") {
    details["cellularGeneration"] = nullptr;
    details["carrier"] = nullptr;
  }
  if (type == "wifi" || type == "ethernet") {
    details["ipAddress"] = orNull(ipAddress);
    details["subnet"] = orNull(subnet);
  }
  state["details"] = std::move(details);
  return state;
}

namespace {

constexpr const char *kEvent = "netInfo.networkStatusDidChange";
constexpr int kTimeoutMs = 500;

GVariant *property(GDBusConnection *bus, const char *path, const char *iface, const char *name) {
  GVariant *reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.NetworkManager", path, "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", iface, name), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, kTimeoutMs,
      nullptr, nullptr);
  if (!reply) return nullptr;
  GVariant *value = nullptr;
  g_variant_get(reply, "(v)", &value);
  g_variant_unref(reply);
  return value;
}

std::string stringProperty(GDBusConnection *bus, const char *path, const char *iface, const char *name) {
  GVariant *v = property(bus, path, iface, name);
  std::string out;
  if (v && (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH))) {
    out = g_variant_get_string(v, nullptr);
  }
  if (v) g_variant_unref(v);
  return out;
}

std::string subnetMask(guint32 prefix) {
  guint32 mask = prefix == 0 ? 0 : 0xffffffffu << (32 - std::min<guint32>(prefix, 32));
  return std::to_string(mask >> 24) + "." + std::to_string((mask >> 16) & 255) + "." +
         std::to_string((mask >> 8) & 255) + "." + std::to_string(mask & 255);
}

// NetworkManager's view of the primary connection: type, SSID, IPv4.
struct Primary {
  std::string type = "other", ssid, ipAddress, subnet;
};

Primary primaryConnection() {
  Primary out;
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
  if (!bus) return out;
  const char *nm = "/org/freedesktop/NetworkManager";
  std::string active = stringProperty(bus, nm, "org.freedesktop.NetworkManager", "PrimaryConnection");
  std::string nmType = stringProperty(bus, nm, "org.freedesktop.NetworkManager", "PrimaryConnectionType");
  if (active.empty() || active == "/") {
    g_object_unref(bus);
    return out;
  }
  out.type = netInfoType(nmType);
  const char *activeIface = "org.freedesktop.NetworkManager.Connection.Active";
  if (out.type == "wifi") {
    // The SSID: the active connection's device's access point.
    if (GVariant *devices = property(bus, active.c_str(), activeIface, "Devices")) {
      if (g_variant_n_children(devices) > 0) {
        const char *device = nullptr;
        g_variant_get_child(devices, 0, "&o", &device);
        std::string ap = stringProperty(bus, device, "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint");
        if (!ap.empty() && ap != "/") {
          if (GVariant *ssid = property(bus, ap.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Ssid")) {
            gsize n = 0;
            const char *bytes = static_cast<const char *>(g_variant_get_fixed_array(ssid, &n, 1));
            out.ssid.assign(bytes, n);
            g_variant_unref(ssid);
          }
        }
      }
      g_variant_unref(devices);
    }
    if (out.ssid.empty()) out.ssid = stringProperty(bus, active.c_str(), activeIface, "Id");
  }
  std::string ip4 = stringProperty(bus, active.c_str(), activeIface, "Ip4Config");
  if (!ip4.empty() && ip4 != "/") {
    if (GVariant *data = property(bus, ip4.c_str(), "org.freedesktop.NetworkManager.IP4Config", "AddressData")) {
      if (g_variant_n_children(data) > 0) {
        GVariant *first = g_variant_get_child_value(data, 0);
        GVariantDict dict;
        g_variant_dict_init(&dict, first);
        const char *address = nullptr;
        guint32 prefix = 0;
        if (g_variant_dict_lookup(&dict, "address", "&s", &address)) out.ipAddress = address;
        if (g_variant_dict_lookup(&dict, "prefix", "u", &prefix)) out.subnet = subnetMask(prefix);
        g_variant_dict_clear(&dict);
        g_variant_unref(first);
      }
      g_variant_unref(data);
    }
  }
  g_object_unref(bus);
  return out;
}

// `settle`: GIO's portal monitor (in a Flatpak) learns the state a moment
// after it's made, and says "no network" until then: wait up to a second
// for one.
folly::dynamic currentState(bool settle = false) {
  GNetworkMonitor *monitor = g_network_monitor_get_default();
  bool available = g_network_monitor_get_network_available(monitor);
  for (int i = 0; settle && !available && i < 20; i++) {
    g_usleep(50 * 1000);
    available = g_network_monitor_get_network_available(monitor);
  }
  int connectivity = g_network_monitor_get_connectivity(monitor);
  bool metered = g_network_monitor_get_network_metered(monitor);
  Primary p = available ? primaryConnection() : Primary{};
  return netInfoState(available, connectivity, metered, p.type, p.ssid, p.ipAddress, p.subnet);
}

void emit(const std::shared_ptr<CallInvoker> &js, folly::dynamic state) {
  js->invokeAsync([state = std::move(state)](Runtime &rt) {
    auto emitter = rt.global().getProperty(rt, "__rctDeviceEventEmitter");
    if (emitter.isUndefined()) return;
    auto object = emitter.asObject(rt);
    auto fn = object.getPropertyAsFunction(rt, "emit");
    fn.callWithThis(rt, object, facebook::jsi::String::createFromAscii(rt, kEvent),
                    facebook::jsi::valueFromDynamic(rt, state));
  });
}

}  // namespace

// GNetworkMonitor's signals, on the main thread, while JS listens. The
// module owns it; signals go nowhere after the module is gone.
struct Monitor : std::enable_shared_from_this<Monitor> {
  std::shared_ptr<CallInvoker> js;
  std::atomic<int> listeners{0};
  std::atomic<bool> alive{true};
  gulong handlers[3] = {0, 0, 0};
  std::mutex lastMutex;
  std::string last;  // the last state sent (JSON), so repeats aren't

  void changed() {
    if (!alive || listeners <= 0) return;
    auto self = shared_from_this();
    // NetworkManager's D-Bus calls block: off the main thread.
    std::thread([self] {
      // GIO signals a change in several steps: report where it settles.
      g_usleep(250 * 1000);
      folly::dynamic state = currentState();
      std::string json = folly::toJson(state);
      {
        std::lock_guard<std::mutex> lock(self->lastMutex);
        if (json == self->last) return;
        self->last = json;
      }
      if (self->alive) emit(self->js, std::move(state));
    }).detach();
  }

  static void onChanged(GObject *, gpointer, gpointer data) {
    static_cast<Monitor *>(data)->changed();
  }

  void connect() {
    GNetworkMonitor *m = g_network_monitor_get_default();
    handlers[0] = g_signal_connect(m, "network-changed", G_CALLBACK(+[](GNetworkMonitor *, gboolean, gpointer data) {
      static_cast<Monitor *>(data)->changed();
    }), this);
    handlers[1] = g_signal_connect(m, "notify::connectivity", G_CALLBACK(onChanged), this);
    handlers[2] = g_signal_connect(m, "notify::network-metered", G_CALLBACK(onChanged), this);
  }

  void disconnect() {
    GNetworkMonitor *m = g_network_monitor_get_default();
    for (gulong &h : handlers) {
      if (h) g_signal_handler_disconnect(m, h);
      h = 0;
    }
  }
};

NetInfoModule::NetInfoModule(std::shared_ptr<CallInvoker> jsInvoker)
    : CxxModule(kName, jsInvoker), monitor_(std::make_shared<Monitor>()) {
  monitor_->js = std::move(jsInvoker);
  method<&NetInfoModule::configure>("configure");
  method<&NetInfoModule::getCurrentState>("getCurrentState");
  method<&NetInfoModule::addListener>("addListener");
  method<&NetInfoModule::removeListeners>("removeListeners");
  // GIO emits the monitor's signals in the main context it was made in.
  auto *keep = new std::shared_ptr<Monitor>(monitor_);
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto &m = *static_cast<std::shared_ptr<Monitor> *>(data);
        if (m->alive) m->connect();
        return G_SOURCE_REMOVE;
      },
      keep, [](gpointer data) { delete static_cast<std::shared_ptr<Monitor> *>(data); });
}

NetInfoModule::~NetInfoModule() {
  monitor_->alive = false;
  auto *keep = new std::shared_ptr<Monitor>(monitor_);
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::shared_ptr<Monitor> *>(data))->disconnect();
        return G_SOURCE_REMOVE;
      },
      keep, [](gpointer data) { delete static_cast<std::shared_ptr<Monitor> *>(data); });
}

// Reachability URLs and the like are for netinfo's JS fallback, which the
// connectivity check here makes unnecessary.
void NetInfoModule::configure(Runtime &, folly::dynamic) {}

AsyncPromise<folly::dynamic> NetInfoModule::getCurrentState(Runtime &rt, std::optional<std::string>) {
  AsyncPromise<folly::dynamic> promise(rt, jsInvoker_);
  // What JS has now: events only say when it changes (GIO signals the same
  // state several times while it starts up).
  std::thread([promise, monitor = monitor_]() mutable {
    folly::dynamic state = currentState(true);
    {
      std::lock_guard<std::mutex> lock(monitor->lastMutex);
      monitor->last = folly::toJson(state);
    }
    promise.resolve(std::move(state));
  }).detach();
  return promise;
}

void NetInfoModule::addListener(Runtime &, std::string) { monitor_->listeners++; }

void NetInfoModule::removeListeners(Runtime &, double count) {
  monitor_->listeners -= static_cast<int>(count);
  if (monitor_->listeners < 0) monitor_->listeners = 0;
}

}  // namespace rngtk_netinfo
