#include "GtkKeyboardHandler.h"

#include "GtkMountingManager.h"
#include "KeyNames.h"
#include "rn_view.h"

#include <react/renderer/components/view/BaseViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/primitives.h>

#include <algorithm>

using namespace facebook::react;

namespace rngtk {

namespace {

std::shared_ptr<GtkWidget> ref(GtkWidget *widget) {
  if (!widget) return nullptr;
  g_object_ref(widget);
  return std::shared_ptr<GtkWidget>(widget,
                                    [](GtkWidget *w) { g_object_unref(w); });
}

bool modifierMatches(const std::optional<bool> &want, bool have) {
  return !want.has_value() || *want == have;
}

}  // namespace

GtkKeyboardHandler::GtkKeyboardHandler(GtkMountingManager &mountingManager,
                                       GtkWidget *root)
    : mountingManager_(mountingManager), root_(root) {
  controller_ = gtk_event_controller_key_new();
  // Capture: before the focused widget (a TextInput) acts on the key, so
  // onKeyDown sees every key and keyDownEvents can stop GTK's handling.
  gtk_event_controller_set_propagation_phase(controller_, GTK_PHASE_CAPTURE);
  g_signal_connect(controller_, "key-pressed", G_CALLBACK(onKeyPressed), this);
  g_signal_connect(controller_, "key-released", G_CALLBACK(onKeyReleased),
                   this);
  gtk_widget_add_controller(root_, controller_);
  g_signal_connect(root_, "realize", G_CALLBACK(onRealize), this);
  connectWindow();
}

GtkKeyboardHandler::~GtkKeyboardHandler() {
  g_signal_handlers_disconnect_by_data(root_, this);
  g_signal_handlers_disconnect_by_data(controller_, this);
  gtk_widget_remove_controller(root_, controller_);
  if (window_) {
    g_signal_handler_disconnect(window_, focusHandler_);
    g_object_remove_weak_pointer(G_OBJECT(window_),
                                 reinterpret_cast<gpointer *>(&window_));
  }
}

// The window's focus-widget tells us when a view gains or loses focus.
void GtkKeyboardHandler::connectWindow() {
  GtkRoot *root = gtk_widget_get_root(root_);
  if (!root || !GTK_IS_WINDOW(root) || GTK_WINDOW(root) == window_) return;
  if (window_) {
    g_signal_handler_disconnect(window_, focusHandler_);
    g_object_remove_weak_pointer(G_OBJECT(window_),
                                 reinterpret_cast<gpointer *>(&window_));
  }
  window_ = GTK_WINDOW(root);
  g_object_add_weak_pointer(G_OBJECT(window_),
                            reinterpret_cast<gpointer *>(&window_));
  focusHandler_ = g_signal_connect(window_, "notify::focus-widget",
                                   G_CALLBACK(onFocusWidget), this);
}

void GtkKeyboardHandler::onRealize(GtkWidget *, gpointer self) {
  static_cast<GtkKeyboardHandler *>(self)->connectWindow();
}

void GtkKeyboardHandler::onFocusWidget(GObject *, GParamSpec *, gpointer self) {
  static_cast<GtkKeyboardHandler *>(self)->focusChanged();
}

GtkKeyboardHandler::Target GtkKeyboardHandler::focusTarget() const {
  GtkRoot *root = gtk_widget_get_root(root_);
  GtkWidget *focus = root ? gtk_root_get_focus(root) : nullptr;
  if (!focus || (focus != root_ && !gtk_widget_is_ancestor(focus, root_))) {
    return {};
  }
  for (GtkWidget *w = focus; w; w = gtk_widget_get_parent(w)) {
    auto t = mountingManager_.targetForView(w);
    if (t.tag != 0) return Target{t.tag, t.emitter, w};
    if (w == root_) break;
  }
  return {};
}

void GtkKeyboardHandler::focusChanged() {
  // Only a focused View gets onFocus here; TextInput sends its own, and
  // focus inside a View's child (a TextInput) doesn't focus the View.
  GtkRoot *root = gtk_widget_get_root(root_);
  GtkWidget *focus = root ? gtk_root_get_focus(root) : nullptr;
  bool ours = focus && RN_IS_VIEW(focus) && focus != root_ &&
              gtk_widget_is_ancestor(focus, root_) &&
              mountingManager_.targetForView(focus).tag != 0;
  GtkWidget *now = ours ? focus : nullptr;
  if (now == focusedView_.get()) return;
  if (focusedView_) {
    auto emitter = std::dynamic_pointer_cast<const BaseViewEventEmitter>(
        mountingManager_.targetForView(focusedView_.get()).emitter);
    if (emitter) emitter->onBlur();
  }
  focusedView_ = ref(now);
  activationKeycode_ = 0;
  if (now) {
    auto emitter = std::dynamic_pointer_cast<const BaseViewEventEmitter>(
        mountingManager_.targetForView(now).emitter);
    if (emitter) emitter->onFocus();
  }
}

bool GtkKeyboardHandler::isHandled(const Target &target, const std::string &key,
                                   const std::string &code,
                                   GdkModifierType state, bool down) const {
  bool alt = state & GDK_ALT_MASK, ctrl = state & GDK_CONTROL_MASK,
       shift = state & GDK_SHIFT_MASK,
       meta = state & (GDK_META_MASK | GDK_SUPER_MASK);
  // The focused view, then its ancestors, as a key bubbles.
  for (GtkWidget *w = target.widget; w; w = gtk_widget_get_parent(w)) {
    auto t = mountingManager_.targetForView(w);
    if (t.tag != 0) {
      auto props = std::dynamic_pointer_cast<const ViewProps>(
          mountingManager_.propsForTag(t.tag));
      if (props) {
        for (const auto &e : down ? props->keyDownEvents : props->keyUpEvents) {
          if (e.key.empty() && e.code.empty()) continue;
          if ((e.key.empty() || e.key == key) &&
              (e.code.empty() || e.code == code) &&
              modifierMatches(e.altKey, alt) && modifierMatches(e.ctrlKey, ctrl) &&
              modifierMatches(e.metaKey, meta) &&
              modifierMatches(e.shiftKey, shift)) {
            return true;
          }
        }
      }
    }
    if (w == root_) break;
  }
  return false;
}

void GtkKeyboardHandler::dispatchKey(const char *type, const Target &target,
                                     const std::string &key,
                                     const std::string &code,
                                     GdkModifierType state, bool down,
                                     bool repeat) {
  if (!target.emitter) return;
  // GDK's state is from before the event; a modifier key's own flag is
  // set while it's down, as in browsers.
  bool alt = state & GDK_ALT_MASK, ctrl = state & GDK_CONTROL_MASK,
       shift = state & GDK_SHIFT_MASK,
       meta = state & (GDK_META_MASK | GDK_SUPER_MASK);
  if (key == "Alt") alt = down;
  if (key == "Control") ctrl = down;
  if (key == "Shift") shift = down;
  if (key == "Meta") meta = down;
  folly::dynamic payload = folly::dynamic::object("key", key)("code", code)(
      "altKey", alt)("ctrlKey", ctrl)("metaKey", meta)("shiftKey", shift)(
      "repeat", repeat)("isComposing", false);
  target.emitter->dispatchEvent(type, std::move(payload),
                                RawEvent::Category::Discrete);
}

// The focused view itself (not a TextInput in it) presses on Enter and
// Space when it listens for clicks (Pressable, the Touchables, Button).
bool GtkKeyboardHandler::canActivate(const Target &target,
                                     GdkModifierType state) const {
  if (!target.widget || !RN_IS_VIEW(target.widget) ||
      target.widget != focusedView_.get()) {
    return false;
  }
  if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_META_MASK | GDK_SUPER_MASK)) {
    return false;
  }
  return mountingManager_.hasEventListener(
      target.tag, static_cast<size_t>(ViewEvents::Offset::Click));
}

void GtkKeyboardHandler::activate(const Target &target) {
  // No pointerType: Pressability's onClick takes it as a keyboard or
  // accessibility click and calls onPress.
  if (target.emitter) {
    target.emitter->dispatchEvent("click", folly::dynamic::object(),
                                  RawEvent::Category::Discrete);
  }
}

bool GtkKeyboardHandler::keyPressed(guint keyval, guint keycode,
                                    GdkModifierType state) {
  bool repeat = !held_.insert(keycode).second;
  Target target = focusTarget();
  if (target.tag == 0) return false;
  std::string key = w3cKey(keyval), code = w3cCode(keycode);
  dispatchKey("keyDown", target, key, code, state, true, repeat);
  if (isHandled(target, key, code, state, true)) return true;
  // The Menu key and Shift+F10 open the focused view's context menu (or
  // its nearest ancestor's), pointing at it.
  GdkModifierType mods = GdkModifierType(state & gtk_accelerator_get_default_mod_mask());
  if (!repeat && ((keyval == GDK_KEY_Menu && mods == 0) ||
                  (keyval == GDK_KEY_F10 && mods == GDK_SHIFT_MASK))) {
    if (mountingManager_.showContextMenu(root_, target.widget, -1, -1)) return true;
  }
  if (canActivate(target, state)) {
    if (key == "Enter") {
      if (!repeat) activate(target);
      return true;
    }
    if (key == " ") {
      // Like a button: Space presses on release.
      activationKeycode_ = keycode;
      return true;
    }
  }
  return false;
}

void GtkKeyboardHandler::keyReleased(guint keyval, guint keycode,
                                     GdkModifierType state) {
  held_.erase(keycode);
  Target target = focusTarget();
  bool activation = activationKeycode_ != 0 && activationKeycode_ == keycode;
  if (activation) activationKeycode_ = 0;
  if (target.tag == 0) return;
  dispatchKey("keyUp", target, w3cKey(keyval), w3cCode(keycode), state, false,
              false);
  if (activation && canActivate(target, GdkModifierType(0))) activate(target);
}

gboolean GtkKeyboardHandler::onKeyPressed(GtkEventControllerKey *, guint keyval,
                                          guint keycode, GdkModifierType state,
                                          gpointer self) {
  return static_cast<GtkKeyboardHandler *>(self)->keyPressed(keyval, keycode,
                                                             state);
}

void GtkKeyboardHandler::onKeyReleased(GtkEventControllerKey *, guint keyval,
                                       guint keycode, GdkModifierType state,
                                       gpointer self) {
  static_cast<GtkKeyboardHandler *>(self)->keyReleased(keyval, keycode, state);
}

}  // namespace rngtk
