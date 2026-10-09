#include "Notifications.h"

#include "CxxModule.h"
#include "Dialogs.h"
#include "PlatformModules.h"

#include <glog/logging.h>
#include <gtk/gtk.h>

#include <atomic>

using namespace facebook::react;
using facebook::jsi::Runtime;

namespace rngtk {

namespace {

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

std::string str(const folly::dynamic &d, const char *key) {
  if (!d.isObject()) return "";
  auto it = d.find(key);
  if (it == d.items().end()) return "";
  if (it->second.isString()) return it->second.getString();
  if (it->second.isNumber()) return std::to_string(it->second.asInt());
  return "";
}

constexpr const char *kAction = "rngtk-notification";

using WeakState = std::weak_ptr<PlatformState>;

// The app action notifications activate: (id, action). It raises the
// app's window and tells JS. Its state lives with the GtkApplication.
void ensure_action(GtkApplication *app, const std::shared_ptr<PlatformState> &state) {
  if (g_action_map_lookup_action(G_ACTION_MAP(app), kAction)) return;
  GSimpleAction *action = g_simple_action_new(kAction, G_VARIANT_TYPE("(ss)"));
  g_signal_connect_data(
      action, "activate",
      G_CALLBACK(+[](GSimpleAction *, GVariant *parameter, gpointer data) {
        auto *weak = static_cast<WeakState *>(data);
        auto state = weak->lock();
        if (!state || !parameter) return;
        const char *id = nullptr;
        const char *what = nullptr;
        g_variant_get(parameter, "(&s&s)", &id, &what);
        if (GtkWindow *window = dialogParent(*state)) gtk_window_present(window);
        if (state->emitDeviceEvent) {
          state->emitDeviceEvent(folly::dynamic::array(
              "rngtkNotification", folly::dynamic::object("id", id)("action", what)));
        }
      }),
      new WeakState(state), [](gpointer data, GClosure *) { delete static_cast<WeakState *>(data); },
      GConnectFlags(0));
  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
  g_object_unref(action);
}

// GNOME's own notifications want an installed .desktop file for the app
// id; without one, GLib's freedesktop backend (set before the first
// notification creates the backend). An explicit GNOTIFICATION_BACKEND
// wins.
void choose_backend(const std::string &appId) {
  static bool chosen = false;
  if (chosen) return;
  chosen = true;
  if (g_getenv("GNOTIFICATION_BACKEND") || g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) ||
      g_getenv("SNAP")) {
    return;
  }
  // Where desktops look for it: $XDG_DATA_HOME and $XDG_DATA_DIRS.
  std::string name = "/applications/" + appId + ".desktop";
  if (g_file_test((g_get_user_data_dir() + name).c_str(), G_FILE_TEST_EXISTS)) return;
  for (const char *const *dir = g_get_system_data_dirs(); *dir; dir++) {
    if (g_file_test((std::string(*dir) + name).c_str(), G_FILE_TEST_EXISTS)) return;
  }
  g_setenv("GNOTIFICATION_BACKEND", "freedesktop", TRUE);
}

GIcon *icon_for(const std::string &icon) {
  if (icon.empty()) return nullptr;
  if (icon.find('/') != std::string::npos || icon.rfind("file:", 0) == 0) {
    GFile *file = g_file_new_for_commandline_arg(icon.c_str());
    GIcon *out = g_file_icon_new(file);
    g_object_unref(file);
    return out;
  }
  return g_themed_icon_new(icon.c_str());
}

class NotificationsModule : public CxxModule<NotificationsModule> {
 public:
  NotificationsModule(std::shared_ptr<CallInvoker> jsInvoker, std::shared_ptr<PlatformState> state)
      : CxxModule("LinuxNotifications", std::move(jsInvoker)), state_(std::move(state)) {
    method<&NotificationsModule::show>("show");
    method<&NotificationsModule::close>("close");
  }

  // options: {id, title, body, icon, priority: 'low' | 'normal' | 'high' |
  // 'urgent', buttons: [{id, title}]}. Returns the id (made up if none).
  std::string show(Runtime &, folly::dynamic options) {
    static std::atomic<int> next{1};
    std::string id = str(options, "id");
    if (id.empty()) id = "n" + std::to_string(next++);
    on_main([state = state_, id, options = std::move(options)] {
      GtkWindow *window = state->window ? state->window() : nullptr;
      GtkApplication *app = window ? gtk_window_get_application(window) : nullptr;
      if (!app || !g_application_get_application_id(G_APPLICATION(app))) {
        LOG(WARNING) << "Notifications: the app has no id to send them with";
        return;
      }
      choose_backend(g_application_get_application_id(G_APPLICATION(app)));
      ensure_action(app, state);
      std::string title = str(options, "title");
      GNotification *n = g_notification_new(title.empty() ? " " : title.c_str());
      std::string body = str(options, "body");
      if (!body.empty()) g_notification_set_body(n, body.c_str());
      if (GIcon *icon = icon_for(str(options, "icon"))) {
        g_notification_set_icon(n, icon);
        g_object_unref(icon);
      }
      std::string priority = str(options, "priority");
      g_notification_set_priority(n, priority == "low"      ? G_NOTIFICATION_PRIORITY_LOW
                                      : priority == "high"   ? G_NOTIFICATION_PRIORITY_HIGH
                                      : priority == "urgent" ? G_NOTIFICATION_PRIORITY_URGENT
                                                             : G_NOTIFICATION_PRIORITY_NORMAL);
      std::string action = std::string("app.") + kAction;
      g_notification_set_default_action_and_target(n, action.c_str(), "(ss)", id.c_str(), "default");
      if (options.isObject() && options.count("buttons") && options["buttons"].isArray()) {
        int i = 0;
        for (const auto &button : options["buttons"]) {
          std::string bid = str(button, "id");
          if (bid.empty()) bid = std::to_string(i);
          i++;
          g_notification_add_button_with_target(n, str(button, "title").c_str(), action.c_str(),
                                                "(ss)", id.c_str(), bid.c_str());
        }
      }
      g_application_send_notification(G_APPLICATION(app), id.c_str(), n);
      g_object_unref(n);
    });
    return id;
  }

  void close(Runtime &, std::string id) {
    on_main([state = state_, id] {
      GtkWindow *window = state->window ? state->window() : nullptr;
      if (GtkApplication *app = window ? gtk_window_get_application(window) : nullptr) {
        g_application_withdraw_notification(G_APPLICATION(app), id.c_str());
      }
    });
  }

 private:
  std::shared_ptr<PlatformState> state_;
};

}  // namespace

std::shared_ptr<TurboModule> makeNotificationsModule(const std::string &name,
                                                     const std::shared_ptr<CallInvoker> &jsInvoker,
                                                     const std::shared_ptr<PlatformState> &state) {
  if (name == "LinuxNotifications") return std::make_shared<NotificationsModule>(jsInvoker, state);
  return nullptr;
}

}  // namespace rngtk
