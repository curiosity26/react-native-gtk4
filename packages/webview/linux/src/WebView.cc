// react-native-webview on GTK4: RNCWebView as a WebKitGTK 6.0
// WebKitWebView, driven by the library's iOS JS (the same native
// component on iOS and macOS), and RNCWebViewModule.
//
// Props: newSource ({uri, html, baseUrl, headers}; GET only), injectedJavaScript
// (at document end), injectedJavaScriptBeforeContentLoaded (at document
// start), messagingEnabled (window.ReactNativeWebView.postMessage), userAgent,
// applicationNameForUserAgent, javaScriptEnabled, incognito (an ephemeral
// session), webviewDebuggingEnabled (the inspector), mediaPlaybackRequires-
// UserAction, allowFileAccessFromFileURLs, allowUniversalAccessFromFileURLs,
// javaScriptCanOpenWindowsAutomatically.
//
// Events: loadingStart, loadingProgress, loadingFinish, loadingError,
// httpError, message, shouldStartLoadWithRequest (the navigation waits for
// JS's answer through RNCWebViewModule.shouldStartLoadWithLockIdentifier, as
// on iOS), openWindow.
//
// Commands: goBack, goForward, reload, stopLoading, injectJavaScript,
// postMessage, requestFocus, loadUrl, clearCache, clearHistory.
#include <folly/json.h>
#include <glib.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <rngtk/CxxModule.h>
#include <rngtk/Extensions.h>
#include <webkit/webkit.h>

#include <atomic>
#include <map>
#include <mutex>

using namespace facebook::react;

namespace rngtk_webview {

// ---- The component's C++ side ----------------------------------------------

// The props as the JS sends them, merged across updates (one class for the
// many props the library has, which change rarely).
class WebViewProps final : public ViewProps {
 public:
  WebViewProps() = default;
  WebViewProps(const PropsParserContext &context, const WebViewProps &sourceProps,
               const RawProps &rawProps)
      : ViewProps(context, sourceProps, rawProps), raw(sourceProps.raw) {
    folly::dynamic changed = rawProps.toDynamic();
    if (changed.isObject()) {
      for (auto &[key, value] : changed.items()) raw[key] = value;
    }
  }
  folly::dynamic raw = folly::dynamic::object;
};

class WebViewEventEmitter : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;
  // "loadingStart" reaches JS as onLoadingStart.
  void emit(const std::string &type, folly::dynamic payload) const {
    dispatchEvent(type, std::move(payload));
  }
};

extern const char WebViewComponentName[] = "RNCWebView";
using WebViewShadowNode = ConcreteViewShadowNode<WebViewComponentName, WebViewProps, WebViewEventEmitter>;
using WebViewComponentDescriptor = ConcreteComponentDescriptor<WebViewShadowNode>;

// ---- Navigation locks (onShouldStartLoadWithRequest) ----------------------

namespace {

// Decisions waiting for JS, by lock identifier. Main thread only.
std::map<int, WebKitPolicyDecision *> pending;
std::atomic<int> nextLock{1};

void decide(int lock, bool allow) {
  auto it = pending.find(lock);
  if (it == pending.end()) return;
  WebKitPolicyDecision *decision = it->second;
  pending.erase(it);
  if (allow) webkit_policy_decision_use(decision);
  else webkit_policy_decision_ignore(decision);
  g_object_unref(decision);
}

// ---- The widget -------------------------------------------------------------

struct State {
  std::shared_ptr<const WebViewEventEmitter> emitter;
  folly::dynamic props = folly::dynamic::object;
  std::string loadedSource;  // newSource, as JSON: what's loaded
  WebKitUserContentManager *content = nullptr;
};

constexpr const char *kState = "rngtk-webview";

State *stateOf(gpointer widget) {
  return static_cast<State *>(g_object_get_data(G_OBJECT(widget), kState));
}

std::string str(const folly::dynamic &props, const char *key) {
  const auto *v = props.get_ptr(key);
  return v && v->isString() ? v->asString() : "";
}

bool flag(const folly::dynamic &props, const char *key, bool fallback) {
  const auto *v = props.get_ptr(key);
  return v && v->isBool() ? v->asBool() : fallback;
}

// What every event says about the page (WebViewNativeEvent).
folly::dynamic pageEvent(WebKitWebView *view, int lock = 0) {
  const char *uri = webkit_web_view_get_uri(view);
  const char *title = webkit_web_view_get_title(view);
  return folly::dynamic::object("url", uri ? uri : "")("title", title ? title : "")(
      "loading", bool(webkit_web_view_is_loading(view)))("canGoBack", bool(webkit_web_view_can_go_back(view)))(
      "canGoForward", bool(webkit_web_view_can_go_forward(view)))("lockIdentifier", lock);
}

void emit(WebKitWebView *view, const char *type, folly::dynamic payload) {
  State *s = stateOf(view);
  if (s && s->emitter) s->emitter->emit(type, std::move(payload));
}

// window.ReactNativeWebView.postMessage → onMessage.
constexpr const char *kBridge =
    "window.ReactNativeWebView = window.ReactNativeWebView || {"
    "  postMessage: function (data) {"
    "    window.webkit.messageHandlers.ReactNativeWebView.postMessage(String(data));"
    "  }"
    "};";

void addScript(WebKitUserContentManager *content, const std::string &source,
               WebKitUserScriptInjectionTime when, bool mainFrameOnly) {
  if (source.empty()) return;
  WebKitUserScript *script = webkit_user_script_new(
      source.c_str(), mainFrameOnly ? WEBKIT_USER_CONTENT_INJECT_TOP_FRAME : WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      when, nullptr, nullptr);
  webkit_user_content_manager_add_script(content, script);
  webkit_user_script_unref(script);
}

// The page scripts: the bridge, and the injected ones (they run on every
// page load, as on iOS).
void applyScripts(State *s) {
  webkit_user_content_manager_remove_all_scripts(s->content);
  const folly::dynamic &p = s->props;
  if (flag(p, "messagingEnabled", false)) {
    addScript(s->content, kBridge, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, false);
  }
  addScript(s->content, str(p, "injectedJavaScriptBeforeContentLoaded"), WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            flag(p, "injectedJavaScriptBeforeContentLoadedForMainFrameOnly", true));
  addScript(s->content, str(p, "injectedJavaScript"), WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
            flag(p, "injectedJavaScriptForMainFrameOnly", true));
}

void applySettings(WebKitWebView *view, const folly::dynamic &p) {
  WebKitSettings *settings = webkit_web_view_get_settings(view);
  webkit_settings_set_enable_javascript(settings, flag(p, "javaScriptEnabled", true));
  webkit_settings_set_javascript_can_open_windows_automatically(
      settings, flag(p, "javaScriptCanOpenWindowsAutomatically", false));
  webkit_settings_set_media_playback_requires_user_gesture(settings,
                                                           flag(p, "mediaPlaybackRequiresUserAction", true));
  webkit_settings_set_allow_file_access_from_file_urls(settings, flag(p, "allowFileAccessFromFileURLs", false));
  webkit_settings_set_allow_universal_access_from_file_urls(settings,
                                                            flag(p, "allowUniversalAccessFromFileURLs", false));
  webkit_settings_set_enable_developer_extras(settings, flag(p, "webviewDebuggingEnabled", false));
  std::string agent = str(p, "userAgent");
  if (!agent.empty()) {
    webkit_settings_set_user_agent(settings, agent.c_str());
  } else {
    std::string app = str(p, "applicationNameForUserAgent");
    webkit_settings_set_user_agent_with_application_details(settings, app.empty() ? nullptr : app.c_str(), nullptr);
  }
}

void load(WebKitWebView *view, const folly::dynamic &source) {
  if (!source.isObject()) return;
  std::string html = str(source, "html");
  if (const auto *h = source.get_ptr("html"); h && h->isString()) {
    std::string base = str(source, "baseUrl");
    webkit_web_view_load_html(view, html.c_str(), base.empty() ? "about:blank" : base.c_str());
    return;
  }
  std::string uri = str(source, "uri");
  if (uri.empty()) return;
  WebKitURIRequest *request = webkit_uri_request_new(uri.c_str());
  if (const auto *headers = source.get_ptr("headers"); headers && headers->isArray()) {
    SoupMessageHeaders *h = webkit_uri_request_get_http_headers(request);
    for (const auto &header : *headers) {
      std::string name = str(header, "name"), value = str(header, "value");
      if (h && !name.empty()) soup_message_headers_replace(h, name.c_str(), value.c_str());
    }
  }
  // (WebKitGTK loads requests as GET: no method or body.)
  webkit_web_view_load_request(view, request);
  g_object_unref(request);
}

GtkWidget *create(const ShadowView &view) {
  folly::dynamic props = folly::dynamic::object;
  if (auto p = std::dynamic_pointer_cast<const WebViewProps>(view.props)) props = p->raw;
  // Incognito: a session of its own, kept in memory.
  WebKitNetworkSession *session =
      flag(props, "incognito", false) ? webkit_network_session_new_ephemeral() : nullptr;
  WebKitUserContentManager *content = webkit_user_content_manager_new();
  GtkWidget *widget = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "user-content-manager", content,
                                              session ? "network-session" : nullptr, session, nullptr));
  if (session) g_object_unref(session);
  auto *state = new State();
  state->content = content;  // the view holds it
  g_object_unref(content);
  g_object_set_data_full(G_OBJECT(widget), kState, state, [](gpointer p) { delete static_cast<State *>(p); });
  WebKitWebView *web = WEBKIT_WEB_VIEW(widget);

  webkit_user_content_manager_register_script_message_handler(content, "ReactNativeWebView", nullptr);
  g_signal_connect(content, "script-message-received::ReactNativeWebView",
                   G_CALLBACK(+[](WebKitUserContentManager *, JSCValue *value, gpointer data) {
                     auto *view = WEBKIT_WEB_VIEW(data);
                     gchar *text = jsc_value_to_string(value);
                     folly::dynamic event = pageEvent(view);
                     event["data"] = text ? text : "";
                     g_free(text);
                     emit(view, "message", std::move(event));
                   }),
                   widget);

  g_signal_connect(widget, "load-changed", G_CALLBACK(+[](WebKitWebView *view, WebKitLoadEvent event, gpointer) {
                     folly::dynamic e = pageEvent(view);
                     e["navigationType"] = "other";
                     e["mainDocumentURL"] = e["url"];
                     if (event == WEBKIT_LOAD_STARTED) emit(view, "loadingStart", std::move(e));
                     if (event == WEBKIT_LOAD_FINISHED) emit(view, "loadingFinish", std::move(e));
                   }),
                   nullptr);
  g_signal_connect(widget, "notify::estimated-load-progress", G_CALLBACK(+[](WebKitWebView *view, GParamSpec *, gpointer) {
                     folly::dynamic e = pageEvent(view);
                     e["progress"] = webkit_web_view_get_estimated_load_progress(view);
                     emit(view, "loadingProgress", std::move(e));
                   }),
                   nullptr);
  g_signal_connect(widget, "load-failed",
                   G_CALLBACK(+[](WebKitWebView *view, WebKitLoadEvent, gchar *uri, GError *error, gpointer) -> gboolean {
                     // Stopping a load isn't an error.
                     if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) return FALSE;
                     folly::dynamic e = pageEvent(view);
                     e["url"] = uri ? uri : "";
                     e["domain"] = g_quark_to_string(error->domain);
                     e["code"] = error->code;
                     e["description"] = error->message;
                     emit(view, "loadingError", std::move(e));
                     return FALSE;
                   }),
                   nullptr);
  g_signal_connect(widget, "decide-policy",
                   G_CALLBACK(+[](WebKitWebView *view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type,
                                  gpointer) -> gboolean {
                     if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
                       // HTTP errors of the main document: onHttpError.
                       auto *response = WEBKIT_RESPONSE_POLICY_DECISION(decision);
                       WebKitURIResponse *r = webkit_response_policy_decision_get_response(response);
                       guint status = webkit_uri_response_get_status_code(r);
                       if (status >= 400 && webkit_response_policy_decision_is_main_frame_main_resource(response)) {
                         folly::dynamic e = pageEvent(view);
                         e["statusCode"] = status;
                         e["description"] = "";
                         emit(view, "httpError", std::move(e));
                       }
                       return FALSE;
                     }
                     auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
                     WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(navigation);
                     WebKitURIRequest *request = webkit_navigation_action_get_request(action);
                     const char *uri = webkit_uri_request_get_uri(request);
                     if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
                       folly::dynamic e = pageEvent(view);
                       e["targetUrl"] = uri ? uri : "";
                       emit(view, "openWindow", std::move(e));
                       webkit_policy_decision_ignore(decision);
                       return TRUE;
                     }
                     // Ask JS (useWebViewLogic's onShouldStartLoadWithRequest),
                     // which answers through RNCWebViewModule.
                     State *s = stateOf(view);
                     if (!s || !s->emitter) return FALSE;
                     int lock = nextLock++;
                     pending[lock] = WEBKIT_POLICY_DECISION(g_object_ref(decision));
                     folly::dynamic e = pageEvent(view, lock);
                     e["url"] = uri ? uri : "";
                     e["navigationType"] = webkit_navigation_action_get_navigation_type(action) ==
                                                   WEBKIT_NAVIGATION_TYPE_LINK_CLICKED
                                               ? "click"
                                               : "other";
                     e["isTopFrame"] = webkit_navigation_action_is_redirect(action) ? false : true;
                     e["mainDocumentURL"] = uri ? uri : "";
                     emit(view, "shouldStartLoadWithRequest", std::move(e));
                     // If JS never answers (it reloaded, say), go ahead.
                     g_timeout_add_seconds(10, [](gpointer l) -> gboolean {
                       decide(GPOINTER_TO_INT(l), true);
                       return G_SOURCE_REMOVE;
                     }, GINT_TO_POINTER(lock));
                     return TRUE;
                   }),
                   nullptr);
  return widget;
}

void update(GtkWidget *widget, const ShadowView &, const ShadowView &newView) {
  State *s = stateOf(widget);
  auto props = std::dynamic_pointer_cast<const WebViewProps>(newView.props);
  if (!s || !props) return;
  s->emitter = std::static_pointer_cast<const WebViewEventEmitter>(newView.eventEmitter);
  WebKitWebView *view = WEBKIT_WEB_VIEW(widget);
  s->props = props->raw;
  applySettings(view, s->props);
  applyScripts(s);
  const folly::dynamic *source = s->props.get_ptr("newSource");
  std::string json = source ? folly::toJson(*source) : "";
  if (json != s->loadedSource) {
    s->loadedSource = json;
    load(view, *source);
  }
}

void command(GtkWidget *widget, const std::string &name, const folly::dynamic &args) {
  WebKitWebView *view = WEBKIT_WEB_VIEW(widget);
  auto arg = [&](size_t i) { return args.isArray() && args.size() > i && args[i].isString() ? args[i].asString() : std::string(); };
  if (name == "goBack") webkit_web_view_go_back(view);
  else if (name == "goForward") webkit_web_view_go_forward(view);
  else if (name == "reload") webkit_web_view_reload(view);
  else if (name == "stopLoading") webkit_web_view_stop_loading(view);
  else if (name == "requestFocus") gtk_widget_grab_focus(widget);
  else if (name == "loadUrl") webkit_web_view_load_uri(view, arg(0).c_str());
  else if (name == "injectJavaScript") {
    std::string js = arg(0);
    webkit_web_view_evaluate_javascript(view, js.c_str(), gssize(js.size()), nullptr, nullptr, nullptr, nullptr, nullptr);
  } else if (name == "postMessage") {
    // As on iOS: a MessageEvent on window (and document).
    std::string data = folly::toJson(folly::dynamic(arg(0)));
    std::string js = "(function(){var e=new MessageEvent('message',{data:" + data +
                     "});window.dispatchEvent(e);document.dispatchEvent(e);})();";
    webkit_web_view_evaluate_javascript(view, js.c_str(), gssize(js.size()), nullptr, nullptr, nullptr, nullptr, nullptr);
  } else if (name == "clearCache") {
    WebKitNetworkSession *session = webkit_web_view_get_network_session(view);
    WebKitWebsiteDataManager *data = webkit_network_session_get_website_data_manager(session);
    auto types = static_cast<WebKitWebsiteDataTypes>(WEBKIT_WEBSITE_DATA_MEMORY_CACHE | WEBKIT_WEBSITE_DATA_DISK_CACHE);
    webkit_website_data_manager_clear(data, types, 0, nullptr, nullptr, nullptr);
  } else if (name == "clearHistory") {
    // WebKitGTK has no API to clear the back/forward list.
  }
}

}  // namespace

// ---- RNCWebViewModule --------------------------------------------------------

class WebViewModule : public rngtk::CxxModule<WebViewModule> {
 public:
  static constexpr const char *kName = "RNCWebViewModule";
  explicit WebViewModule(std::shared_ptr<CallInvoker> js) : CxxModule(kName, std::move(js)) {
    method<&WebViewModule::isFileUploadSupported>("isFileUploadSupported");
    method<&WebViewModule::shouldStartLoadWithLockIdentifier>("shouldStartLoadWithLockIdentifier");
  }

  // <input type=file> opens GTK's file chooser.
  AsyncPromise<bool> isFileUploadSupported(facebook::jsi::Runtime &rt) {
    AsyncPromise<bool> promise(rt, jsInvoker_);
    promise.resolve(true);
    return promise;
  }

  void shouldStartLoadWithLockIdentifier(facebook::jsi::Runtime &, bool shouldStart, double lock) {
    struct Answer {
      int lock;
      bool allow;
    };
    g_main_context_invoke_full(
        nullptr, G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
          auto *a = static_cast<Answer *>(data);
          decide(a->lock, a->allow);
          return G_SOURCE_REMOVE;
        },
        new Answer{int(lock), shouldStart}, [](gpointer data) { delete static_cast<Answer *>(data); });
  }
};

}  // namespace rngtk_webview

std::shared_ptr<const rngtk::Package> rngtk_webview_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-webview";
  rngtk::NativeComponent component;
  component.descriptor = concreteComponentDescriptorProvider<rngtk_webview::WebViewComponentDescriptor>();
  component.create = rngtk_webview::create;
  component.update = rngtk_webview::update;
  component.command = rngtk_webview::command;
  package->components.push_back(component);
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == rngtk_webview::WebViewModule::kName) {
          return std::make_shared<rngtk_webview::WebViewModule>(jsInvoker);
        }
        return nullptr;
      });
  return package;
}
