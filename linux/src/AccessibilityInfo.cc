#include "AccessibilityInfo.h"

#include <glog/logging.h>

namespace rngtk {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::CallInvoker;
using facebook::react::TurboModule;

AccessibilityStatus::AccessibilityStatus(GdkDisplay *display, bool followSystem,
                                         OnChange onChange)
    : settings_(gtk_settings_get_for_display(display)),
      onChange_(std::move(onChange)) {
  gboolean animations = TRUE;
  g_object_get(settings_, "gtk-enable-animations", &animations, nullptr);
  reduceMotion_ = !animations;
  animationsHandler_ = g_signal_connect(
      settings_, "notify::gtk-enable-animations", G_CALLBACK(onAnimations), this);
  if (!followSystem) return;
  GError *error = nullptr;
  a11yBus_ = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr, "org.a11y.Bus",
      "/org/a11y/bus", "org.a11y.Status", nullptr, &error);
  if (!a11yBus_) {
    LOG(INFO) << "AccessibilityInfo: no AT-SPI bus: " << error->message;
    g_clear_error(&error);
    return;
  }
  g_signal_connect(a11yBus_, "g-properties-changed",
                   G_CALLBACK(onBusProperties), this);
  readScreenReader();
}

AccessibilityStatus::~AccessibilityStatus() {
  if (animationsHandler_) g_signal_handler_disconnect(settings_, animationsHandler_);
  if (a11yBus_) g_signal_handlers_disconnect_by_data(a11yBus_, this);
  g_clear_object(&a11yBus_);
}

void AccessibilityStatus::readScreenReader() {
  GVariant *value =
      g_dbus_proxy_get_cached_property(a11yBus_, "ScreenReaderEnabled");
  bool enabled = value && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) &&
                 g_variant_get_boolean(value);
  if (value) g_variant_unref(value);
  if (enabled == screenReader_) return;
  screenReader_ = enabled;
  if (onChange_) onChange_("screenReaderChanged", enabled);
}

void AccessibilityStatus::onBusProperties(GDBusProxy *, GVariant *, GStrv,
                                          gpointer self) {
  static_cast<AccessibilityStatus *>(self)->readScreenReader();
}

void AccessibilityStatus::onAnimations(GObject *, GParamSpec *, gpointer data) {
  auto *self = static_cast<AccessibilityStatus *>(data);
  gboolean animations = TRUE;
  g_object_get(self->settings_, "gtk-enable-animations", &animations, nullptr);
  bool reduce = !animations;
  if (reduce == self->reduceMotion_) return;
  self->reduceMotion_ = reduce;
  if (self->onChange_) self->onChange_("reduceMotionChanged", reduce);
}

void AccessibilityStatus::setScreenReaderEnabled(bool enabled) {
  if (enabled == screenReader_) return;
  screenReader_ = enabled;
  if (onChange_) onChange_("screenReaderChanged", enabled);
}

namespace {

// Runs `fn` on the GTK main thread.
void on_main(std::function<void()> fn) {
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      new std::function<void()>(std::move(fn)),
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

// getCurrent*State(onSuccess, onError): onSuccess(value), synchronously.
Value succeed(Runtime &rt, const Value *args, size_t count, bool value) {
  if (count > 0 && args[0].isObject() && args[0].asObject(rt).isFunction(rt)) {
    args[0].asObject(rt).asFunction(rt).call(rt, value);
  }
  return Value::undefined();
}

AccessibilityManagerModule &self(TurboModule &module) {
  return static_cast<AccessibilityManagerModule &>(module);
}

}  // namespace

AccessibilityManagerModule::AccessibilityManagerModule(
    std::shared_ptr<CallInvoker> jsInvoker, Host host)
    : TurboModule(kModuleName, std::move(jsInvoker)), host_(std::move(host)) {
  methodMap_["getCurrentVoiceOverState"] = MethodMetadata{
      2, [](Runtime &rt, TurboModule &m, const Value *args, size_t count) {
        auto status = self(m).host_.status.lock();
        return succeed(rt, args, count, status && status->screenReaderEnabled());
      }};
  methodMap_["getCurrentReduceMotionState"] = MethodMetadata{
      2, [](Runtime &rt, TurboModule &m, const Value *args, size_t count) {
        auto status = self(m).host_.status.lock();
        return succeed(rt, args, count, status && status->reduceMotionEnabled());
      }};
  // Not on Linux desktops (or not readable): false.
  for (const char *name :
       {"getCurrentBoldTextState", "getCurrentGrayscaleState",
        "getCurrentInvertColorsState", "getCurrentReduceTransparencyState",
        "getCurrentDarkerSystemColorsState",
        "getCurrentPrefersCrossFadeTransitionsState"}) {
    methodMap_[name] = MethodMetadata{
        2, [](Runtime &rt, TurboModule &, const Value *args, size_t count) {
          return succeed(rt, args, count, false);
        }};
  }
  methodMap_["setAccessibilityContentSizeMultipliers"] = MethodMetadata{
      1, [](Runtime &, TurboModule &, const Value *, size_t) {
        return Value::undefined();
      }};
  methodMap_["setAccessibilityFocus"] = MethodMetadata{
      1, [](Runtime &, TurboModule &m, const Value *args, size_t count) {
        if (count > 0 && args[0].isNumber()) {
          int tag = int(args[0].asNumber());
          on_main([focus = self(m).host_.focus, tag] {
            if (focus) focus(tag);
          });
        }
        return Value::undefined();
      }};
  methodMap_["announceForAccessibility"] = MethodMetadata{
      1, [](Runtime &rt, TurboModule &m, const Value *args, size_t count) {
        if (count > 0 && args[0].isString()) {
          on_main([announce = self(m).host_.announce,
                   text = args[0].asString(rt).utf8(rt)] {
            if (announce) announce(text, GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
          });
        }
        return Value::undefined();
      }};
  // options.priority: 'low', 'default' or 'high' (iOS 17's); `queue` has
  // no GTK equivalent.
  methodMap_["announceForAccessibilityWithOptions"] = MethodMetadata{
      2, [](Runtime &rt, TurboModule &m, const Value *args, size_t count) {
        if (count == 0 || !args[0].isString()) return Value::undefined();
        auto priority = GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM;
        if (count > 1 && args[1].isObject()) {
          auto p = args[1].asObject(rt).getProperty(rt, "priority");
          if (p.isString()) {
            std::string s = p.asString(rt).utf8(rt);
            if (s == "high") priority = GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_HIGH;
            if (s == "low") priority = GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_LOW;
          }
        }
        on_main([announce = self(m).host_.announce,
                 text = args[0].asString(rt).utf8(rt), priority] {
          if (announce) announce(text, priority);
        });
        return Value::undefined();
      }};
}

}  // namespace rngtk
