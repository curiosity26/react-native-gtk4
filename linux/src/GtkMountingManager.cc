#include "GtkMountingManager.h"

#include "GtkViewProps.h"
#include "PangoText.h"
#include "rn_scroll_view.h"
#include "rn_text.h"
#include "rn_text_input.h"
#include "GtkSwitchShadowNode.h"
#include "GtkWindowControlsShadowNode.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/modal/ModalHostViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/FBReactNativeSpec/ComponentDescriptors.h>
#include <react/renderer/components/iostextinput/TextInputComponentDescriptor.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/mounting/MountingTransaction.h>

#include <cstring>

using namespace facebook::react;

namespace rngtk {

namespace {

bool is_paragraph(const ShadowView &view) {
  return std::strcmp(view.componentName, ParagraphComponentName) == 0;
}

GQuark tag_quark() {
  static GQuark q = g_quark_from_static_string("rn-tag");
  return q;
}

// A paragraph's nested <Text> spans: byte ranges in its text and the span's
// tag, for hit-testing presses on nested text.
struct TextSpan {
  int start, end;
  Tag tag;
  SharedEventEmitter emitter;
};
using TextSpans = std::vector<TextSpan>;

GQuark spans_quark() {
  static GQuark q = g_quark_from_static_string("rn-text-spans");
  return q;
}

}  // namespace

GtkMountingManager::GtkMountingManager(OnAfterMount onAfterMount)
    : onAfterMount_(std::move(onAfterMount)),
      mainThread_(std::this_thread::get_id()) {}

GtkMountingManager::~GtkMountingManager() noexcept {
  if (contextMenu_) {
    g_object_remove_weak_pointer(G_OBJECT(contextMenu_), reinterpret_cast<gpointer *>(&contextMenu_));
  }
  closeAllModals();
  for (auto &[tag, widget] : views_) g_object_unref(widget);
}

void GtkMountingManager::registerSurface(SurfaceId surfaceId, GtkWidget *root) {
  roots_[surfaceId] = root;
  // The root view's tag is the surface id.
  views_[surfaceId] = GTK_WIDGET(g_object_ref(root));
  g_object_set_qdata(G_OBJECT(root), tag_quark(), GINT_TO_POINTER(surfaceId));
}

void GtkMountingManager::unregisterSurface(SurfaceId surfaceId) {
  roots_.erase(surfaceId);
  forget(surfaceId);
}

void GtkMountingManager::forget(Tag tag) {
  unmountModal(tag);
  forgetScrollView(tag);
  forgetImage(tag);
  textInputs_.erase(tag);
  shadowViews_.erase(tag);
  if (auto it = views_.find(tag); it != views_.end()) {
    g_object_set_qdata(G_OBJECT(it->second), tag_quark(), nullptr);
    g_object_unref(it->second);
    views_.erase(it);
  }
}

GtkMountingManager::EventTarget GtkMountingManager::targetForView(
    GtkWidget *widget, int textIndex) const {
  if (textIndex >= 0) {
    if (auto *spans = static_cast<TextSpans *>(
            g_object_get_qdata(G_OBJECT(widget), spans_quark()))) {
      for (const auto &span : *spans) {
        if (textIndex >= span.start && textIndex < span.end && span.emitter) {
          return {span.tag, span.emitter};
        }
      }
    }
  }
  Tag tag = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(widget), tag_quark()));
  auto it = shadowViews_.find(tag);
  return {tag, it == shadowViews_.end() ? nullptr : it->second.eventEmitter};
}

bool GtkMountingManager::hasEventListener(Tag tag, size_t offset) const {
  auto it = shadowViews_.find(tag);
  if (it == shadowViews_.end()) return false;
  auto props = std::dynamic_pointer_cast<const ViewProps>(it->second.props);
  return props && props->events.bits[offset];
}

bool GtkMountingManager::isSelectableText(Tag tag) const {
  auto it = shadowViews_.find(tag);
  if (it == shadowViews_.end()) return false;
  auto props = std::dynamic_pointer_cast<const ParagraphProps>(it->second.props);
  return props && props->isSelectable;
}

Props::Shared GtkMountingManager::propsForTag(Tag tag) const {
  auto it = shadowViews_.find(tag);
  return it == shadowViews_.end() ? nullptr : it->second.props;
}

GtkWidget *GtkMountingManager::viewForNativeId(
    const std::string &nativeId) const {
  for (const auto &[tag, view] : shadowViews_) {
    auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
    if (props && props->nativeId == nativeId) return viewForTag(tag);
  }
  return nullptr;
}

GtkWidget *GtkMountingManager::viewForTag(Tag tag) const {
  auto it = views_.find(tag);
  return it == views_.end() ? nullptr : it->second;
}

void GtkMountingManager::executeMount(SurfaceId surfaceId,
                                      MountingTransaction &&transaction) {
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (!onMainThread() || !pending_.empty()) {
      PendingWork work;
      work.surfaceId = surfaceId;
      work.transaction.emplace(std::move(transaction));
      pending_.push_back(std::move(work));
      scheduleFlushLocked();
      return;
    }
  }
  apply(surfaceId, transaction);
}

void GtkMountingManager::runOnMainInOrder(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (!onMainThread() || !pending_.empty()) {
      pending_.push_back(PendingWork{0, std::nullopt, std::move(fn)});
      scheduleFlushLocked();
      return;
    }
  }
  fn();
}

void GtkMountingManager::scheduleFlushLocked() {
  if (flushScheduled_) return;
  flushScheduled_ = true;
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *weak = static_cast<std::weak_ptr<GtkMountingManager> *>(data);
        if (auto self = weak->lock()) self->flushPending();
        return G_SOURCE_REMOVE;
      },
      new std::weak_ptr<GtkMountingManager>(weak_from_this()),
      [](gpointer data) {
        delete static_cast<std::weak_ptr<GtkMountingManager> *>(data);
      });
}

bool GtkMountingManager::isIdle() {
  std::lock_guard<std::mutex> lock(pendingMutex_);
  return pending_.empty();
}

void GtkMountingManager::setUIManager(
    std::weak_ptr<UIManager> /*uiManager*/) noexcept {
  if (onUIManagerChanged_) onUIManagerChanged_();
}

void GtkMountingManager::flushPending() {
  for (;;) {
    std::unique_lock<std::mutex> lock(pendingMutex_);
    if (pending_.empty()) {
      flushScheduled_ = false;
      return;
    }
    PendingWork work = std::move(pending_.front());
    pending_.pop_front();
    lock.unlock();
    if (work.transaction) {
      apply(work.surfaceId, *work.transaction);
    } else if (work.work) {
      work.work();
    }
  }
}

void GtkMountingManager::apply(SurfaceId surfaceId,
                               const MountingTransaction &transaction) {
  for (const auto &m : transaction.getMutations()) {
    switch (m.type) {
      case ShadowViewMutation::Create:
        create(m.newChildShadowView);
        break;
      case ShadowViewMutation::Delete:
        if (roots_.count(m.oldChildShadowView.tag)) break;  // ours to keep
        forget(m.oldChildShadowView.tag);
        break;
      case ShadowViewMutation::Insert: {
        GtkWidget *parent = containerFor(viewForTag(m.parentTag));
        GtkWidget *child = viewForTag(m.newChildShadowView.tag);
        if (child && isModalHost(m.newChildShadowView)) {
          // Not in its parent: in a window of its own.
          mountModal(m.parentTag, m.newChildShadowView.tag);
          break;
        }
        // A library's container component mounts its children itself.
        if (const NativeComponent *native = nativeComponentForTag(m.parentTag);
            parent && child && native && native->insertChild) {
          native->insertChild(parent, child, m.index);
          break;
        }
        if (!parent || !child || !RN_IS_VIEW(parent)) {
          LOG(ERROR) << "Insert: can't mount " << m.newChildShadowView.tag
                     << " into " << m.parentTag;
          break;
        }
        rn_view_insert_child(RN_VIEW(parent), child, m.index);
        applyLayout(child, m.newChildShadowView);
        refreshContentLabels(parent);
        break;
      }
      case ShadowViewMutation::Remove: {
        GtkWidget *parent = containerFor(viewForTag(m.parentTag));
        GtkWidget *child = viewForTag(m.oldChildShadowView.tag);
        if (isModalHost(m.oldChildShadowView)) {
          unmountModal(m.oldChildShadowView.tag);
          break;
        }
        if (const NativeComponent *native = nativeComponentForTag(m.parentTag);
            parent && child && native && native->removeChild) {
          native->removeChild(parent, child);
          break;
        }
        if (parent && child && gtk_widget_get_parent(child) == parent) {
          rn_view_remove_child(RN_VIEW(parent), child);
          refreshContentLabels(parent);
        }
        break;
      }
      case ShadowViewMutation::Update:
        update(m.oldChildShadowView, m.newChildShadowView);
        break;
      default:
        break;
    }
  }
  if (screenReaderActive_) {
    // Where views landed decides which are elements of their own.
    for (const auto &m : transaction.getMutations()) {
      if (m.type == ShadowViewMutation::Insert || m.type == ShadowViewMutation::Update) {
        if (GtkWidget *w = viewForTag(m.newChildShadowView.tag)) updateScreenReaderFocus(w);
      }
    }
  }
  presentPendingModals();
  mountCount_++;
  if (onAfterMount_) onAfterMount_(surfaceId);
}

void GtkMountingManager::create(const ShadowView &view) {
  const char *name = view.componentName;
  bool scroll = std::strcmp(name, "ScrollView") == 0;
  bool textInput = std::strcmp(name, "TextInput") == 0;
  bool toggle = std::strcmp(name, "Switch") == 0;
  GtkWidget *widget;
  const NativeComponent *native = nativeComponentFor(view);
  if (native && native->create) {
    widget = native->create(view);  // a library's (rngtk/Extensions.h)
  } else if (is_paragraph(view)) {
    widget = rn_text_new("");
  } else if (scroll) {
    widget = rn_scroll_view_new();
  } else if (textInput) {
    auto props = std::dynamic_pointer_cast<const BaseTextInputProps>(view.props);
    widget = rn_text_input_new(props && props->multiline);
  } else if (toggle) {
    widget = gtk_switch_new();
  } else if (std::strcmp(name, "ActivityIndicatorView") == 0) {
    widget = gtk_spinner_new();
  } else if (std::strcmp(name, GtkWindowControlsComponentName) == 0) {
    widget = gtk_window_controls_new(GTK_PACK_END);  // updateWindowControls sets the side
  } else if (hasAccessibleValue(view)) {
    widget = rn_range_view_new();  // AT-SPI's Value interface
  } else {
    widget = rn_view_new();
  }
  views_[view.tag] = GTK_WIDGET(g_object_ref_sink(widget));
  g_object_set_qdata(G_OBJECT(widget), tag_quark(), GINT_TO_POINTER(view.tag));
  // GTK fixes the accessible role once the widget is realized.
  if (RN_IS_VIEW(widget) || RN_IS_TEXT(widget)) {
    g_object_set(widget, "accessible-role", accessibleRoleFor(view, widget),
                 nullptr);
  }
  if (scroll) connectScrollView(widget, view.tag);
  if (textInput) connectTextInput(widget, view.tag);
  if (toggle) connectSwitch(widget, view.tag);
  update(ShadowView{}, view);
}

void GtkMountingManager::update(const ShadowView &oldView,
                                const ShadowView &newView) {
  GtkWidget *widget = viewForTag(newView.tag);
  if (!widget) return;
  shadowViews_[newView.tag] = newView;
  if (oldView.props != newView.props ||
      oldView.layoutMetrics != newView.layoutMetrics) {
    applyProps(widget, newView);
  }
  if (isModalTag(newView.tag)) {
    layoutModal(newView.tag);
    updateModal(oldView, newView);
  } else if (!isModalHost(newView)) {
    applyLayout(widget, newView);
  }
  if (RN_IS_TEXT(widget)) applyParagraph(widget, newView);
  if (RN_IS_SCROLL_VIEW(widget)) updateScrollView(widget, oldView, newView);
  if (RN_IS_TEXT_INPUT(widget)) updateTextInput(widget, oldView, newView);
  if (GTK_IS_SWITCH(widget)) updateSwitch(widget, oldView, newView);
  if (GTK_IS_SPINNER(widget)) updateSpinner(widget, oldView, newView);
  if (GTK_IS_WINDOW_CONTROLS(widget)) {
    auto props = std::dynamic_pointer_cast<const GtkWindowControlsProps>(newView.props);
    GtkPackType side = props && props->side == "start" ? GTK_PACK_START : GTK_PACK_END;
    if (gtk_window_controls_get_side(GTK_WINDOW_CONTROLS(widget)) != side) {
      gtk_window_controls_set_side(GTK_WINDOW_CONTROLS(widget), side);
    }
  }
  if (std::strcmp(newView.componentName, ImageComponentName) == 0) {
    updateImage(widget, oldView, newView);
  }
  if (RN_IS_VIEW(widget)) updateFocus(widget, newView);
  updateAccessibility(widget, oldView, newView);
  if (const NativeComponent *native = nativeComponentFor(newView); native && native->update) {
    native->update(widget, oldView, newView);
  }
}

void GtkMountingManager::addNativeComponents(const std::vector<NativeComponent> &components) {
  for (const auto &component : components) {
    if (!component.descriptor.name) continue;
    nativeComponents_[component.descriptor.name] = std::make_shared<NativeComponent>(component);
  }
}

const NativeComponent *GtkMountingManager::nativeComponentFor(const ShadowView &view) const {
  if (nativeComponents_.empty() || !view.componentName) return nullptr;
  auto it = nativeComponents_.find(view.componentName);
  return it == nativeComponents_.end() ? nullptr : it->second.get();
}

const NativeComponent *GtkMountingManager::nativeComponentForTag(facebook::react::Tag tag) const {
  auto it = shadowViews_.find(tag);
  return it == shadowViews_.end() ? nullptr : nativeComponentFor(it->second);
}

void GtkMountingManager::updateFocus(GtkWidget *widget, const ShadowView &view) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  static GQuark done = g_quark_from_static_string("rngtk-auto-focused");
  if (!props || !props->autoFocus || g_object_get_qdata(G_OBJECT(widget), done)) {
    return;
  }
  g_object_set_qdata(G_OBJECT(widget), done, GINT_TO_POINTER(1));
  // Once it's in the window (and laid out).
  g_idle_add_full(
      G_PRIORITY_DEFAULT_IDLE,
      [](gpointer w) -> gboolean {
        if (gtk_widget_get_root(GTK_WIDGET(w))) gtk_widget_grab_focus(GTK_WIDGET(w));
        return G_SOURCE_REMOVE;
      },
      g_object_ref(widget), g_object_unref);
}

// ViewCommands.focus / blur (ref.focus(), ref.blur() on a View).
bool GtkMountingManager::focusCommand(GtkWidget *widget, const std::string &name) {
  if (name == "focus") {
    gtk_widget_grab_focus(widget);
    return true;
  }
  if (name == "blur") {
    GtkRoot *root = gtk_widget_get_root(widget);
    if (root && gtk_root_get_focus(root) == widget) gtk_root_set_focus(root, nullptr);
    return true;
  }
  return false;
}

void GtkMountingManager::applyProps(GtkWidget *widget, const ShadowView &view) {
  if (auto props = std::dynamic_pointer_cast<const ViewProps>(view.props)) {
    apply_view_props(widget, *props, view.layoutMetrics);
    if (RN_IS_SCROLL_VIEW(widget)) {
      apply_view_style(rn_scroll_view_get_background(RN_SCROLL_VIEW(widget)),
                       *props, view.layoutMetrics);
    }
    if (RN_IS_TEXT_INPUT(widget)) {
      apply_view_style(rn_text_input_get_background(RN_TEXT_INPUT(widget)),
                       *props, view.layoutMetrics);
    }
  }
}

void GtkMountingManager::setFontScale(float scale) {
  if (scale == fontScale_) return;
  fontScale_ = scale;
  const ShadowView none{};
  for (const auto &[tag, view] : shadowViews_) {
    GtkWidget *widget = viewForTag(tag);
    if (widget && RN_IS_TEXT_INPUT(widget)) updateTextInput(widget, none, view);
  }
}

void GtkMountingManager::refreshColors() {
  // The mounted props are unchanged; only what their dynamic colors
  // resolve to differs, so each view re-applies its own props.
  const ShadowView none{};
  for (const auto &[tag, view] : shadowViews_) {
    GtkWidget *widget = viewForTag(tag);
    if (!widget) continue;
    applyProps(widget, view);
    if (RN_IS_TEXT(widget)) applyParagraph(widget, view);
    if (RN_IS_TEXT_INPUT(widget)) updateTextInput(widget, none, view);
    if (GTK_IS_SWITCH(widget)) updateSwitch(widget, none, view);
    if (GTK_IS_SPINNER(widget)) updateSpinner(widget, none, view);
    if (std::strcmp(view.componentName, ImageComponentName) == 0 &&
        RN_IS_VIEW(widget)) {
      if (GdkTexture *texture = rn_view_get_image(RN_VIEW(widget))) {
        applyImage(tag, texture);
      }
    }
  }
}

void GtkMountingManager::synchronouslyUpdateViewOnUIThread(
    Tag tag, const folly::dynamic &props) {
  if (!onMainThread()) {
    runOnMainInOrder([weak = weak_from_this(), tag, props] {
      if (auto self = weak.lock()) {
        self->synchronouslyUpdateViewOnUIThread(tag, props);
      }
    });
    return;
  }
  // Native Animated (opacity, transforms...) on the main thread, between
  // commits: clone the mounted props with the animated values and apply.
  auto registry = registry_.lock();
  auto it = shadowViews_.find(tag);
  GtkWidget *widget = viewForTag(tag);
  if (!registry || !contextContainer_ || it == shadowViews_.end() || !widget) {
    return;
  }
  ShadowView &view = it->second;
  const ComponentDescriptor &descriptor = registry->at(view.componentHandle);
  PropsParserContext context{view.surfaceId, *contextContainer_};
  ShadowView oldView = view;
  view.props = descriptor.cloneProps(context, view.props, RawProps(props));
  applyProps(widget, view);
  // A library's component (react-native-svg's elements, say) draws its own
  // props: it gets the animated ones too, each frame, not at the next
  // commit.
  if (const NativeComponent *native = nativeComponentFor(view); native && native->update) {
    native->update(widget, oldView, view);
  }
}

void GtkMountingManager::applyLayout(GtkWidget *widget,
                                     const ShadowView &view) {
  const LayoutMetrics &lm = view.layoutMetrics;
  if (lm == EmptyLayoutMetrics) return;
  // A control can hide itself through its props (a stopped
  // ActivityIndicator); display: none hides anything.
  static GQuark hidden = g_quark_from_static_string("rngtk-hidden-by-props");
  gtk_widget_set_visible(widget, lm.displayType != DisplayType::None &&
                                     !g_object_get_qdata(G_OBJECT(widget), hidden));
  rn_widget_set_frame(widget, lm.frame.origin.x, lm.frame.origin.y,
                      lm.frame.size.width, lm.frame.size.height);
}

void GtkMountingManager::applyParagraph(GtkWidget *widget,
                                        const ShadowView &view) {
  auto state =
      std::dynamic_pointer_cast<const ParagraphShadowNode::ConcreteState>(
          view.state);
  if (!state) return;
  const ParagraphState &data = state->getData();
  // Yoga measured the text inside padding and border.
  const EdgeInsets &in = view.layoutMetrics.contentInsets;
  rn_text_set_insets(RN_TEXT(widget), in.top, in.right, in.bottom, in.left);
  PangoLayout *layout = create_pango_layout(
      gtk_widget_get_pango_context(widget), data.attributedString,
      data.paragraphAttributes,
      view.layoutMetrics.frame.size.width - in.left - in.right);
  rn_text_set_layout(RN_TEXT(widget), layout);
  g_object_unref(layout);
  updateTextAccessibility(widget, view, data.attributedString.getString());

  if (auto props = std::dynamic_pointer_cast<const ParagraphProps>(view.props)) {
    // Selectable text: an I-beam (unless `cursor` says otherwise) and
    // selectionColor's highlight, else the theme's.
    if (props->isSelectable && props->cursor == Cursor::Auto) {
      gtk_widget_set_cursor_from_name(widget, "text");
    }
    if (!props->isSelectable) rn_text_set_selection(RN_TEXT(widget), 0, 0);
    GdkRGBA highlight{};
    if (props->selectionColor && *props->selectionColor) {
      highlight = to_rgba(*props->selectionColor);
    }
    rn_text_set_selection_color(
        RN_TEXT(widget),
        props->selectionColor && *props->selectionColor ? &highlight : nullptr);
  }

  auto *spans = new TextSpans();
  int offset = 0;
  for (const auto &fragment : data.attributedString.getFragments()) {
    int size = static_cast<int>(fragment.string.size());
    spans->push_back({offset, offset + size, fragment.parentShadowView.tag,
                      fragment.parentShadowView.eventEmitter});
    offset += size;
  }
  g_object_set_qdata_full(G_OBJECT(widget), spans_quark(), spans,
                          [](gpointer p) { delete static_cast<TextSpans *>(p); });
}

void GtkMountingManager::dispatchCommand(const ShadowView &shadowView,
                                         const std::string &commandName,
                                         const folly::dynamic &args) {
  if (!onMainThread()) {
    // From the JS thread: after the mounts queued before it.
    runOnMainInOrder([weak = weak_from_this(), shadowView, commandName, args] {
      if (auto self = weak.lock()) {
        self->dispatchCommand(shadowView, commandName, args);
      }
    });
    return;
  }
  GtkWidget *widget = viewForTag(shadowView.tag);
  if (widget && RN_IS_SCROLL_VIEW(widget) &&
      scrollCommand(widget, commandName, args)) {
    return;
  }
  if (widget && RN_IS_TEXT_INPUT(widget) &&
      textInputCommand(widget, shadowView.tag, commandName, args)) {
    return;
  }
  if (widget && GTK_IS_SWITCH(widget) && switchCommand(widget, commandName, args)) {
    return;
  }
  if (widget && RN_IS_VIEW(widget) && focusCommand(widget, commandName)) return;
  if (const NativeComponent *native = nativeComponentFor(shadowView);
      widget && native && native->command) {
    native->command(widget, commandName, args);
    return;
  }
  LOG(WARNING) << "Unsupported command " << commandName << " for "
               << shadowView.componentName;
}

ComponentRegistryFactory GtkMountingManager::getComponentRegistryFactory() {
  return [weak = weak_from_this()](
             const EventDispatcher::Weak &eventDispatcher,
             const std::shared_ptr<const ContextContainer> &contextContainer) {
    // Built once (it outlives the registries made from it), with the
    // libraries' components.
    auto self = weak.lock();
    static std::shared_ptr<ComponentDescriptorProviderRegistry> fallback;
    std::shared_ptr<ComponentDescriptorProviderRegistry> &cached = self ? self->providers_ : fallback;
    if (!cached) cached = [&] {
      auto registry = std::make_shared<ComponentDescriptorProviderRegistry>();
      registry->add(
          concreteComponentDescriptorProvider<ImageComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<ParagraphComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<ScrollViewComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<RawTextComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<TextComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<ViewComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<
                    ModalHostViewComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<TextInputComponentDescriptor>());
      registry->add(
          concreteComponentDescriptorProvider<GtkSwitchComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<
                    GtkWindowControlsComponentDescriptor>());
      registry->add(concreteComponentDescriptorProvider<
                    ActivityIndicatorViewComponentDescriptor>());
      if (self) {
        for (const auto &[name, component] : self->nativeComponents_) {
          registry->add(component->descriptor);
        }
      }
      return registry;
    }();
    auto registry = cached->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
    if (self) {
      self->registry_ = registry;
      self->contextContainer_ = contextContainer;
    }
    return registry;
  };
}

}  // namespace rngtk
