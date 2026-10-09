#include "PlatformModules.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>
#include <glog/logging.h>
#include <react/bridging/Promise.h>

#include <clocale>

using namespace facebook::react;
using facebook::jsi::Runtime;

namespace rngtk {

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

// ---- Linking ------------------------------------------------------------

bool in_sandbox() {
  return g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS) || g_getenv("SNAP");
}

struct Launch {
  AsyncPromise<> promise;
  std::string url;
};

void finish_launch(Launch *launch, bool ok, GError *error) {
  if (ok) {
    launch->promise.resolve();
  } else {
    launch->promise.reject(Error("Unable to open URL: " + launch->url + " (" +
                                 (error ? error->message : "failed") + ")"));
  }
  g_clear_error(&error);
  delete launch;
}

class LinkingModule : public NativeLinkingManagerCxxSpec<LinkingModule> {
 public:
  LinkingModule(std::shared_ptr<CallInvoker> jsInvoker,
                std::shared_ptr<PlatformState> state)
      : NativeLinkingManagerCxxSpec(std::move(jsInvoker)),
        state_(std::move(state)) {}

  AsyncPromise<std::optional<std::string>> getInitialURL(Runtime &rt) {
    AsyncPromise<std::optional<std::string>> promise(rt, jsInvoker_);
    std::lock_guard<std::mutex> lock(state_->mutex);
    promise.resolve(state_->initialURL);
    return promise;
  }

  // Whether the desktop has a handler for the URL's scheme (a file: URL:
  // whether the file exists).
  AsyncPromise<bool> canOpenURL(Runtime &rt, std::string url) {
    AsyncPromise<bool> promise(rt, jsInvoker_);
    on_main([promise, url]() mutable {
      char *scheme = g_uri_parse_scheme(url.c_str());
      bool can = false;
      if (scheme && g_ascii_strcasecmp(scheme, "file") == 0) {
        GFile *file = g_file_new_for_uri(url.c_str());
        can = g_file_query_exists(file, nullptr);
        g_object_unref(file);
      } else if (scheme) {
        GAppInfo *info = g_app_info_get_default_for_uri_scheme(scheme);
        can = info != nullptr;
        g_clear_object(&info);
      }
      g_free(scheme);
      promise.resolve(can);
    });
    return promise;
  }

  // The desktop's default handler for the URL. GIO launches it with GDK's
  // launch context (startup notification, and the activation token that
  // lets it take focus on Wayland). In a Flatpak or Snap, GtkUriLauncher
  // asks the OpenURI portal; GTK 4.14's goes to the portal outside a
  // sandbox too, which can't see handlers only this app's environment
  // has, so it isn't used there.
  AsyncPromise<> openURL(Runtime &rt, std::string url) {
    AsyncPromise<> promise(rt, jsInvoker_);
    on_main([promise, url, state = state_]() mutable {
      char *scheme = g_uri_parse_scheme(url.c_str());
      if (!scheme) {
        promise.reject(Error("Unable to open URL: " + url));
        return;
      }
      g_free(scheme);
      if (state->openURLOverride && state->openURLOverride(url)) {
        promise.resolve();
        return;
      }
      auto *launch = new Launch{promise, url};
      GtkWindow *window = state->window ? state->window() : nullptr;
      if (in_sandbox()) {
        GtkUriLauncher *launcher = gtk_uri_launcher_new(url.c_str());
        gtk_uri_launcher_launch(
            launcher, window, nullptr,
            [](GObject *source, GAsyncResult *result, gpointer data) {
              GError *error = nullptr;
              bool ok = gtk_uri_launcher_launch_finish(GTK_URI_LAUNCHER(source),
                                                       result, &error);
              finish_launch(static_cast<Launch *>(data), ok, error);
              g_object_unref(source);
            },
            launch);
        return;
      }
      GdkDisplay *display = window ? gtk_widget_get_display(GTK_WIDGET(window))
                                   : gdk_display_get_default();
      GdkAppLaunchContext *context = gdk_display_get_app_launch_context(display);
      g_app_info_launch_default_for_uri_async(
          url.c_str(), G_APP_LAUNCH_CONTEXT(context), nullptr,
          [](GObject *, GAsyncResult *result, gpointer data) {
            GError *error = nullptr;
            bool ok = g_app_info_launch_default_for_uri_finish(result, &error);
            finish_launch(static_cast<Launch *>(data), ok, error);
          },
          launch);
      g_object_unref(context);
    });
    return promise;
  }

  AsyncPromise<> openSettings(Runtime &rt) {
    AsyncPromise<> promise(rt, jsInvoker_);
    promise.reject(Error("Linking.openSettings: Linux apps have no settings page"));
    return promise;
  }

  // 'url' events go through RCTDeviceEventEmitter.
  void addListener(Runtime &, std::string) {}
  void removeListeners(Runtime &, double) {}

 private:
  std::shared_ptr<PlatformState> state_;
};

// ---- Clipboard -----------------------------------------------------------

class ClipboardModule : public NativeClipboardCxxSpec<ClipboardModule> {
 public:
  explicit ClipboardModule(std::shared_ptr<CallInvoker> jsInvoker)
      : NativeClipboardCxxSpec(std::move(jsInvoker)) {}

  facebook::jsi::Object getConstants(Runtime &rt) { return facebook::jsi::Object(rt); }

  AsyncPromise<std::string> getString(Runtime &rt) {
    AsyncPromise<std::string> promise(rt, jsInvoker_);
    on_main([promise] {
      gdk_clipboard_read_text_async(
          gdk_display_get_clipboard(gdk_display_get_default()), nullptr,
          [](GObject *source, GAsyncResult *result, gpointer data) {
            auto *p = static_cast<AsyncPromise<std::string> *>(data);
            char *text =
                gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
            p->resolve(text ? text : "");  // empty, as on iOS, if not text
            g_free(text);
            delete p;
          },
          new AsyncPromise<std::string>(promise));
    });
    return promise;
  }

  void setString(Runtime &, std::string content) {
    on_main([content] {
      gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()),
                             content.c_str());
    });
  }
};

// ---- Vibration -------------------------------------------------------------

class VibrationModule : public NativeVibrationCxxSpec<VibrationModule> {
 public:
  explicit VibrationModule(std::shared_ptr<CallInvoker> jsInvoker)
      : NativeVibrationCxxSpec(std::move(jsInvoker)) {}
  facebook::jsi::Object getConstants(Runtime &rt) { return facebook::jsi::Object(rt); }
  // Desktops don't vibrate.
  void vibrate(Runtime &, double) {}
  void vibrateByPattern(Runtime &, facebook::jsi::Array, double) {}
  void cancel(Runtime &) {}
};

// ---- I18nManager ------------------------------------------------------------

class I18nModule : public NativeI18nManagerCxxSpec<I18nModule> {
 public:
  I18nModule(std::shared_ptr<CallInvoker> jsInvoker,
             std::shared_ptr<I18nSettings> settings)
      : NativeI18nManagerCxxSpec(std::move(jsInvoker)),
        settings_(std::move(settings)) {}

  facebook::jsi::Object getConstants(Runtime &rt) {
    facebook::jsi::Object constants(rt);
    constants.setProperty(rt, "isRTL", settings_->isRTL());
    constants.setProperty(rt, "doLeftAndRightSwapInRTL",
                          settings_->doLeftAndRightSwapInRTL());
    constants.setProperty(
        rt, "localeIdentifier",
        facebook::jsi::String::createFromUtf8(rt, settings_->localeIdentifier()));
    return constants;
  }
  // Saved; they apply when the app reloads or starts again, as on iOS.
  void allowRTL(Runtime &, bool allow) { settings_->setAllowRTL(allow); }
  void forceRTL(Runtime &, bool force) { settings_->setForceRTL(force); }
  void swapLeftAndRightInRTL(Runtime &, bool swap) {
    settings_->setSwapLeftAndRight(swap);
  }

 private:
  std::shared_ptr<I18nSettings> settings_;
};

// ---- AppState -------------------------------------------------------------

class AppStateModule : public NativeAppStateCxxSpec<AppStateModule> {
 public:
  AppStateModule(std::shared_ptr<CallInvoker> jsInvoker,
                 std::shared_ptr<PlatformState> state)
      : NativeAppStateCxxSpec(std::move(jsInvoker)), state_(std::move(state)) {}

  facebook::jsi::Object getConstants(Runtime &rt) {
    facebook::jsi::Object constants(rt);
    constants.setProperty(rt, "initialAppState", appStateName(state_->appState));
    return constants;
  }

  void getCurrentAppState(Runtime &rt, facebook::jsi::Function success,
                          facebook::jsi::Function) {
    facebook::jsi::Object data(rt);
    data.setProperty(rt, "app_state", appStateName(state_->appState));
    success.call(rt, data);
  }

  // appStateDidChange and appStateFocusChange go through
  // RCTDeviceEventEmitter.
  void addListener(Runtime &, std::string) {}
  void removeListeners(Runtime &, double) {}

 private:
  std::shared_ptr<PlatformState> state_;
};

}  // namespace

const char *appStateName(int state) {
  return state == 0 ? "active" : state == 1 ? "inactive" : "background";
}

// ---- I18nSettings -----------------------------------------------------------

I18nSettings::I18nSettings(const std::string &appId) {
  // GTK picks the default direction from the locale's translations.
  localeRTL_ = gtk_widget_get_default_direction() == GTK_TEXT_DIR_RTL;
  const char *const *languages = g_get_language_names();
  locale_ = languages && languages[0] ? languages[0] : "C";
  if (auto dot = locale_.find('.'); dot != std::string::npos) locale_.resize(dot);
  gchar *path = g_build_filename(g_get_user_config_dir(), "react-native-gtk4",
                                 appId.c_str(), "i18n.ini", nullptr);
  path_ = path;
  g_free(path);
  GKeyFile *file = g_key_file_new();
  if (g_key_file_load_from_file(file, path_.c_str(), G_KEY_FILE_NONE, nullptr)) {
    auto read = [&](const char *key, bool &out) {
      GError *error = nullptr;
      gboolean value = g_key_file_get_boolean(file, "I18n", key, &error);
      if (!error) out = value;
      g_clear_error(&error);
    };
    read("allowRTL", allowRTL_);
    read("forceRTL", forceRTL_);
    read("swapLeftAndRightInRTL", swap_);
  }
  g_key_file_unref(file);
}

bool I18nSettings::isRTL() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return forceRTL_ || (allowRTL_ && localeRTL_);
}

bool I18nSettings::doLeftAndRightSwapInRTL() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return swap_;
}

void I18nSettings::setAllowRTL(bool allow) {
  std::lock_guard<std::mutex> lock(mutex_);
  allowRTL_ = allow;
  save();
}

void I18nSettings::setForceRTL(bool force) {
  std::lock_guard<std::mutex> lock(mutex_);
  forceRTL_ = force;
  save();
}

void I18nSettings::setSwapLeftAndRight(bool swap) {
  std::lock_guard<std::mutex> lock(mutex_);
  swap_ = swap;
  save();
}

void I18nSettings::save() {
  GKeyFile *file = g_key_file_new();
  g_key_file_set_boolean(file, "I18n", "allowRTL", allowRTL_);
  g_key_file_set_boolean(file, "I18n", "forceRTL", forceRTL_);
  g_key_file_set_boolean(file, "I18n", "swapLeftAndRightInRTL", swap_);
  gchar *dir = g_path_get_dirname(path_.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  GError *error = nullptr;
  if (!g_key_file_save_to_file(file, path_.c_str(), &error)) {
    LOG(WARNING) << "I18nManager: can't save " << path_ << ": " << error->message;
    g_clear_error(&error);
  }
  g_key_file_unref(file);
}

std::shared_ptr<TurboModule> makePlatformModule(
    const std::string &name, const std::shared_ptr<CallInvoker> &jsInvoker,
    const std::shared_ptr<PlatformState> &state) {
  if (name == LinkingModule::kModuleName) {
    return std::make_shared<LinkingModule>(jsInvoker, state);
  }
  if (name == ClipboardModule::kModuleName) {
    return std::make_shared<ClipboardModule>(jsInvoker);
  }
  if (name == VibrationModule::kModuleName) {
    return std::make_shared<VibrationModule>(jsInvoker);
  }
  if (name == I18nModule::kModuleName) {
    return std::make_shared<I18nModule>(jsInvoker, state->i18n);
  }
  if (name == AppStateModule::kModuleName) {
    return std::make_shared<AppStateModule>(jsInvoker, state);
  }
  return nullptr;
}

}  // namespace rngtk
