// <Modal> (Fabric's ModalHostView): its content shows in a GTK window of
// its own, transient for (and modal over) the window its parent is in.
//
// - presentationStyle fullScreen / overFullScreen: an undecorated window
//   the size of the parent's content. The window paints nothing itself:
//   the Modal's container draws its background ('white', backdropColor, or
//   nothing with `transparent`, where the parent shows through). Without a
//   compositor (X11) nothing can show through, so the window paints the
//   window background dimmed instead.
// - pageSheet / formSheet: a dialog-sized, decorated, resizable window
//   with a close button; the content follows its size.
// - animationType: 'fade' fades the window in and out; 'slide' slides the
//   content up from the bottom (and back down). Off when GTK's animations
//   are.
// - onShow once it is shown (after its animation); onRequestClose for
//   Escape and the window's close button (the window stays: the app decides);
//   visible={false} plays the way out, hides the window and sends onDismiss,
//   after which JS unmounts it (Modal.linux.js keeps iOS's order).
// - Screen readers see a modal dialog named by accessibilityLabel (or the
//   parent window's title). The window takes focus and gives it back to the
//   parent when it closes; a modal opened from a modal stacks over it.
//
// The content is the ModalHostView's own RNView, a root for input with its
// own pointer and keyboard handlers. Its size reaches Yoga through the
// view's state (ModalHostViewState::screenSize), as on Android.
#include "GtkMountingManager.h"

#include "GtkKeyboardHandler.h"
#include "GtkPointerHandler.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/renderer/components/modal/ModalHostViewShadowNode.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace facebook::react;

namespace rngtk {

namespace {

constexpr gint64 kAnimationUs = 250000;
// formSheet and pageSheet, as on an iPad, within the parent window.
constexpr int kFormSheetWidth = 540, kFormSheetHeight = 620;
constexpr int kPageSheetWidth = 720;

bool animations_enabled(GtkWidget *widget) {
  gboolean enabled = TRUE;
  g_object_get(gtk_widget_get_settings(widget), "gtk-enable-animations", &enabled,
               nullptr);
  return enabled;
}

void install_modal_css(GdkDisplay *display) {
  static GQuark done = g_quark_from_static_string("rngtk-modal-css");
  if (g_object_get_qdata(G_OBJECT(display), done)) return;
  g_object_set_qdata(G_OBJECT(display), done, GINT_TO_POINTER(1));
  GtkCssProvider *css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      css,
      "window.rngtk-modal-clear { background: none; box-shadow: none; }\n"
      "window.rngtk-modal-dim { background-color: mix(@window_bg_color, black, 0.45); }\n");
  gtk_style_context_add_provider_for_display(
      display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
}

double ease_out(double t) { return 1 - std::pow(1 - t, 3); }

}  // namespace

struct GtkMountingManager::ModalWindow {
  Tag tag = 0;
  GtkWidget *window = nullptr;     // ours; destroyed on unmount
  GtkWidget *container = nullptr;  // the window's child, holds `content`
  GtkWidget *content = nullptr;    // the ModalHostView's RNView (views_)
  std::unique_ptr<GtkPointerHandler> pointer;
  std::unique_ptr<GtkKeyboardHandler> keyboard;
  bool sheet = false;
  bool fade = false, slide = false;
  // The size given to Yoga (state), and the content's slide offset.
  float width = 0, height = 0;
  float offset = 0;
  bool presented = false;  // the window was shown
  bool showSent = false;   // onShow
  bool dismissing = false; // visible={false}: on its way out
  guint tick = 0;
  gint64 animationStart = 0;
  GdkFrameClock *clock = nullptr;
  gulong layoutHandler = 0;
};

bool GtkMountingManager::isModalHost(const ShadowView &view) {
  return std::strcmp(view.componentName, ModalHostViewComponentName) == 0;
}

bool GtkMountingManager::isModalTag(Tag tag) const { return modals_.count(tag) > 0; }

GtkWindow *GtkMountingManager::modalWindow(Tag tag) const {
  auto it = modals_.find(tag);
  return it == modals_.end() ? nullptr : GTK_WINDOW(it->second->window);
}

std::vector<Tag> GtkMountingManager::modalTags() const {
  std::vector<Tag> tags;
  for (const auto &[tag, modal] : modals_) tags.push_back(tag);
  std::sort(tags.begin(), tags.end());
  return tags;
}

GtkPointerHandler *GtkMountingManager::modalPointerHandler(Tag tag) const {
  auto it = modals_.find(tag);
  return it == modals_.end() ? nullptr : it->second->pointer.get();
}

GtkKeyboardHandler *GtkMountingManager::modalKeyboardHandler(Tag tag) const {
  auto it = modals_.find(tag);
  return it == modals_.end() ? nullptr : it->second->keyboard.get();
}

// Insert: the modal's content goes into a window of its own once the
// transaction is applied (its parent may not be in a window before then).
void GtkMountingManager::mountModal(Tag parentTag, Tag tag) {
  pendingModals_.emplace_back(parentTag, tag);
}

void GtkMountingManager::presentPendingModals() {
  // A modal inside another one mounted in the same transaction has a window
  // to open over once the outer one has opened: go round until none does.
  auto pending = std::move(pendingModals_);
  pendingModals_.clear();
  for (bool opened = true; opened && !pending.empty();) {
    opened = false;
    for (auto it = pending.begin(); it != pending.end();) {
      auto [parentTag, tag] = *it;
      GtkWidget *parent = viewForTag(parentTag);
      auto view = shadowViews_.find(tag);
      if (modals_.count(tag) || !parent || !viewForTag(tag) || view == shadowViews_.end()) {
        it = pending.erase(it);
        continue;
      }
      GtkRoot *root = gtk_widget_get_root(parent);
      if (!root || !GTK_IS_WINDOW(root)) {
        ++it;
        continue;
      }
      it = pending.erase(it);
      openModal(GTK_WINDOW(root), tag, view->second);
      opened = true;
    }
  }
  for (auto [parentTag, tag] : pending) {
    LOG(WARNING) << "Modal " << tag << ": its parent " << parentTag << " is not in a window";
  }
}

void GtkMountingManager::openModal(GtkWindow *parentWindow, Tag tag,
                                   const ShadowView &view) {
  auto props = std::static_pointer_cast<const ModalHostViewProps>(view.props);
  auto modal = std::make_shared<ModalWindow>();
  ModalWindow &m = *modal;
  m.tag = tag;
  m.content = viewForTag(tag);
  m.sheet = props->presentationStyle == ModalHostViewPresentationStyle::PageSheet ||
            props->presentationStyle == ModalHostViewPresentationStyle::FormSheet;
  bool animate = animations_enabled(GTK_WIDGET(parentWindow));
  GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(parentWindow));
  bool composited = gdk_display_is_composited(display);
  m.fade = animate && composited &&
           props->animationType == ModalHostViewAnimationType::Fade;
  m.slide = animate && props->animationType == ModalHostViewAnimationType::Slide;
  install_modal_css(display);

  // The parent's content area: the window's child (below a title bar).
  GtkWidget *parentContent = gtk_window_get_child(parentWindow);
  int pw = parentContent ? gtk_widget_get_width(parentContent) : 0;
  int ph = parentContent ? gtk_widget_get_height(parentContent) : 0;
  if (pw <= 0 || ph <= 0) {
    pw = gtk_widget_get_width(GTK_WIDGET(parentWindow));
    ph = gtk_widget_get_height(GTK_WIDGET(parentWindow));
  }
  if (pw <= 0 || ph <= 0) gtk_window_get_default_size(parentWindow, &pw, &ph);
  int w = pw, h = ph;
  if (props->presentationStyle == ModalHostViewPresentationStyle::FormSheet) {
    w = std::min(pw, kFormSheetWidth);
    h = std::min(ph, kFormSheetHeight);
  } else if (props->presentationStyle == ModalHostViewPresentationStyle::PageSheet) {
    w = std::min(pw, kPageSheetWidth);
    h = std::max(std::min(ph, 200), ph - 48);
  }

  m.window = GTK_WIDGET(g_object_new(GTK_TYPE_WINDOW, "accessible-role",
                                     GTK_ACCESSIBLE_ROLE_DIALOG, nullptr));
  GtkWindow *window = GTK_WINDOW(m.window);
  gtk_window_set_transient_for(window, parentWindow);
  gtk_window_set_modal(window, TRUE);
  gtk_window_set_destroy_with_parent(window, TRUE);
  if (GtkApplication *app = gtk_window_get_application(parentWindow)) {
    gtk_window_set_application(window, app);
  }
  std::string title = props->accessibilityLabel;
  if (title.empty()) {
    const char *parentTitle = gtk_window_get_title(parentWindow);
    title = parentTitle ? parentTitle : "";
  }
  gtk_window_set_title(window, title.c_str());
  gtk_accessible_update_property(GTK_ACCESSIBLE(window), GTK_ACCESSIBLE_PROPERTY_MODAL,
                                 TRUE, GTK_ACCESSIBLE_PROPERTY_LABEL, title.c_str(),
                                 -1);
  int titlebarHeight = 0;
  if (m.sheet) {
    gtk_window_set_resizable(window, TRUE);
    // A title bar of our own, measured now, so the content gets the
    // sheet's size from the start.
    GtkWidget *titlebar = gtk_header_bar_new();
    gtk_window_set_titlebar(window, titlebar);
    gtk_widget_measure(titlebar, GTK_ORIENTATION_VERTICAL, w, nullptr, &titlebarHeight,
                       nullptr, nullptr);
  } else {
    gtk_window_set_decorated(window, FALSE);
    gtk_window_set_resizable(window, FALSE);
    // The Modal's container paints; the parent shows through `transparent`.
    gtk_widget_add_css_class(m.window, composited ? "rngtk-modal-clear"
                                       : props->transparent ? "rngtk-modal-dim"
                                                            : "background");
  }
  gtk_window_set_default_size(window, w, h + titlebarHeight);

  m.container = rn_view_new();
  gtk_window_set_child(window, m.container);
  rn_view_insert_child(RN_VIEW(m.container), m.content, 0);
  m.width = float(w);
  m.height = float(h);
  m.offset = m.slide ? m.height : 0;
  rn_widget_set_frame(m.content, 0, m.offset, m.width, m.height);
  m.pointer = std::make_unique<GtkPointerHandler>(*this, m.content);
  m.keyboard = std::make_unique<GtkKeyboardHandler>(*this, m.content);

  // Escape and the close button ask JS (onRequestClose); the window stays.
  // (Bubble phase: a focused view's onKeyDown / keyDownEvents see it first.)
  GtkEventController *keys = gtk_event_controller_key_new();
  g_object_set_data(G_OBJECT(keys), "rngtk-modal-tag", GINT_TO_POINTER(tag));
  g_signal_connect(keys, "key-pressed",
                   G_CALLBACK(+[](GtkEventControllerKey *controller, guint keyval,
                                  guint, GdkModifierType state, gpointer data) -> gboolean {
                     if (keyval != GDK_KEY_Escape ||
                         (state & gtk_accelerator_get_default_mod_mask())) {
                       return FALSE;
                     }
                     Tag t = GPOINTER_TO_INT(
                         g_object_get_data(G_OBJECT(controller), "rngtk-modal-tag"));
                     static_cast<GtkMountingManager *>(data)->requestModalClose(t);
                     return TRUE;
                   }),
                   this);
  gtk_widget_add_controller(m.window, keys);
  g_object_set_data(G_OBJECT(m.window), "rngtk-modal-tag", GINT_TO_POINTER(tag));
  g_signal_connect(m.window, "close-request",
                   G_CALLBACK(+[](GtkWindow *w, gpointer data) -> gboolean {
                     Tag t = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "rngtk-modal-tag"));
                     static_cast<GtkMountingManager *>(data)->requestModalClose(t);
                     return TRUE;
                   }),
                   this);
  // The content follows the window (a resized sheet, or a size the window
  // manager chose).
  g_signal_connect(m.window, "realize",
                   G_CALLBACK(+[](GtkWidget *w, gpointer data) {
                     Tag t = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "rngtk-modal-tag"));
                     static_cast<GtkMountingManager *>(data)->followModalSize(t);
                   }),
                   this);

  modals_[tag] = std::move(modal);
  // Yoga lays the content out at the window's size.
  setModalSize(tag, float(w), float(h));
  if (props->visible) presentModal(tag);
}

void GtkMountingManager::setModalSize(Tag tag, float width, float height) {
  auto it = modals_.find(tag);
  auto view = shadowViews_.find(tag);
  if (it == modals_.end() || view == shadowViews_.end()) return;
  ModalWindow &m = *it->second;
  m.width = width;
  m.height = height;
  rn_widget_set_frame(m.content, 0, m.offset, width, height);
  auto state = std::dynamic_pointer_cast<const ModalHostViewShadowNode::ConcreteState>(
      view->second.state);
  if (!state) return;
  Size size = state->getData().screenSize;
  if (size.width == Float(width) && size.height == Float(height)) return;
  state->updateState(ModalHostViewState(Size{.width = width, .height = height}));
}

void GtkMountingManager::followModalSize(Tag tag) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  ModalWindow &m = *it->second;
  GdkFrameClock *clock = gtk_widget_get_frame_clock(m.window);
  if (!clock || clock == m.clock) return;
  if (m.clock) g_signal_handler_disconnect(m.clock, m.layoutHandler);
  g_set_object(&m.clock, clock);
  struct Data {
    std::weak_ptr<GtkMountingManager> self;
    Tag tag;
  };
  m.layoutHandler = g_signal_connect_data(
      clock, "layout",
      G_CALLBACK(+[](GdkFrameClock *, gpointer p) {
        auto *d = static_cast<Data *>(p);
        auto self = d->self.lock();
        if (!self) return;
        auto it = self->modals_.find(d->tag);
        if (it == self->modals_.end()) return;
        ModalWindow &m = *it->second;
        int w = gtk_widget_get_width(m.container);
        int h = gtk_widget_get_height(m.container);
        if (w > 0 && h > 0 && (float(w) != m.width || float(h) != m.height)) {
          self->setModalSize(d->tag, float(w), float(h));
        }
      }),
      new Data{weak_from_this(), tag},
      [](gpointer p, GClosure *) { delete static_cast<Data *>(p); }, G_CONNECT_AFTER);
}

void GtkMountingManager::presentModal(Tag tag) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  ModalWindow &m = *it->second;
  m.dismissing = false;
  m.presented = true;
  if (m.fade) gtk_widget_set_opacity(m.window, 0);
  gtk_window_present(GTK_WINDOW(m.window));
  // Focus moves in, to the first focusable view (screen readers read the
  // dialog, then it); the parent window keeps its own focus for later.
  if (!gtk_root_get_focus(GTK_ROOT(m.window))) {
    gtk_widget_child_focus(m.content, GTK_DIR_TAB_FORWARD);
  }
  startModalAnimation(tag);
}

void GtkMountingManager::startModalAnimation(Tag tag) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  ModalWindow &m = *it->second;
  m.animationStart = 0;
  if (m.tick) return;
  struct Data {
    std::weak_ptr<GtkMountingManager> self;
    Tag tag;
  };
  m.tick = gtk_widget_add_tick_callback(
      m.window,
      [](GtkWidget *, GdkFrameClock *clock, gpointer p) -> gboolean {
        auto *d = static_cast<Data *>(p);
        auto self = d->self.lock();
        if (!self) return G_SOURCE_REMOVE;
        return self->stepModalAnimation(d->tag, gdk_frame_clock_get_frame_time(clock))
                   ? G_SOURCE_CONTINUE
                   : G_SOURCE_REMOVE;
      },
      new Data{weak_from_this(), tag}, [](gpointer p) { delete static_cast<Data *>(p); });
}

// One frame of the way in or out; false when it's over.
bool GtkMountingManager::stepModalAnimation(Tag tag, gint64 now) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return false;
  ModalWindow &m = *it->second;
  if (!m.animationStart) m.animationStart = now;
  double t = (m.fade || m.slide)
                 ? std::clamp(double(now - m.animationStart) / kAnimationUs, 0.0, 1.0)
                 : 1.0;
  double shown = m.dismissing ? 1 - ease_out(t) : ease_out(t);
  if (m.fade) gtk_widget_set_opacity(m.window, shown);
  if (m.slide) {
    m.offset = float((1 - shown) * m.height);
    rn_widget_set_frame(m.content, 0, m.offset, m.width, m.height);
  }
  if (t < 1) return true;
  m.tick = 0;
  m.offset = 0;
  auto view = shadowViews_.find(tag);
  auto emitter = view == shadowViews_.end()
                     ? nullptr
                     : std::dynamic_pointer_cast<const ModalHostViewEventEmitter>(
                           view->second.eventEmitter);
  if (m.dismissing) {
    gtk_widget_set_visible(m.window, FALSE);
    m.presented = false;
    m.showSent = false;
    if (emitter) emitter->onDismiss({});
  } else if (!m.showSent) {
    m.showSent = true;
    if (emitter) emitter->onShow({});
  }
  return false;
}

void GtkMountingManager::dismissModal(Tag tag) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  ModalWindow &m = *it->second;
  if (m.dismissing) return;
  m.dismissing = true;
  if (!m.presented) {
    m.animationStart = 0;
    stepModalAnimation(tag, g_get_monotonic_time() + kAnimationUs * 2);
    return;
  }
  startModalAnimation(tag);
}

void GtkMountingManager::requestModalClose(Tag tag) {
  auto view = shadowViews_.find(tag);
  if (view == shadowViews_.end() || modals_.count(tag) == 0) return;
  if (modals_[tag]->dismissing) return;
  if (auto emitter = std::dynamic_pointer_cast<const ModalHostViewEventEmitter>(
          view->second.eventEmitter)) {
    emitter->onRequestClose({});
  }
}

void GtkMountingManager::updateModal(const ShadowView &oldView, const ShadowView &newView) {
  auto it = modals_.find(newView.tag);
  if (it == modals_.end()) return;
  auto oldProps = std::dynamic_pointer_cast<const ModalHostViewProps>(oldView.props);
  auto props = std::static_pointer_cast<const ModalHostViewProps>(newView.props);
  bool wasVisible = oldProps && oldProps->visible;
  if (props->visible && (!wasVisible || it->second->dismissing)) {
    presentModal(newView.tag);
  } else if (!props->visible && wasVisible) {
    dismissModal(newView.tag);
  }
  if (!oldProps || oldProps->accessibilityLabel != props->accessibilityLabel) {
    if (!props->accessibilityLabel.empty()) {
      gtk_window_set_title(GTK_WINDOW(it->second->window), props->accessibilityLabel.c_str());
      gtk_accessible_update_property(GTK_ACCESSIBLE(it->second->window),
                                     GTK_ACCESSIBLE_PROPERTY_LABEL,
                                     props->accessibilityLabel.c_str(), -1);
    }
  }
}

// The modal's content keeps its own frame: the window's size, at the
// slide's offset.
void GtkMountingManager::layoutModal(Tag tag) {
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  ModalWindow &m = *it->second;
  rn_widget_set_frame(m.content, 0, m.offset, m.width, m.height);
}

// Remove (or teardown): the window goes; the content stays mounted until
// its Delete.
void GtkMountingManager::unmountModal(Tag tag) {
  pendingModals_.erase(std::remove_if(pendingModals_.begin(), pendingModals_.end(),
                                      [tag](const auto &p) { return p.second == tag; }),
                       pendingModals_.end());
  auto it = modals_.find(tag);
  if (it == modals_.end()) return;
  std::shared_ptr<ModalWindow> modal = std::move(it->second);
  modals_.erase(it);
  ModalWindow &m = *modal;
  if (m.clock) {
    g_signal_handler_disconnect(m.clock, m.layoutHandler);
    g_clear_object(&m.clock);
  }
  if (m.tick) gtk_widget_remove_tick_callback(m.window, m.tick);
  g_signal_handlers_disconnect_by_data(m.window, this);
  m.pointer.reset();
  m.keyboard.reset();
  if (gtk_widget_get_parent(m.content) == m.container) {
    rn_view_remove_child(RN_VIEW(m.container), m.content);
  }
  // Modals opened from this one go first (destroy-with-parent would take
  // their windows, but not our records of them).
  std::vector<Tag> children;
  for (const auto &[t, other] : modals_) {
    if (gtk_window_get_transient_for(GTK_WINDOW(other->window)) == GTK_WINDOW(m.window)) {
      children.push_back(t);
    }
  }
  for (Tag t : children) unmountModal(t);
  gtk_window_destroy(GTK_WINDOW(m.window));
}

void GtkMountingManager::closeAllModals() {
  while (!modals_.empty()) unmountModal(modals_.begin()->first);
}

}  // namespace rngtk
