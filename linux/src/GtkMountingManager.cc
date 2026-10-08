#include "GtkMountingManager.h"

#include "PangoText.h"
#include "rn_text.h"
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
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/MountingTransaction.h>

#include <cstring>

using namespace facebook::react;

namespace rngtk {

namespace {

GdkRGBA to_rgba(const SharedColor &color) {
  if (!color) return GdkRGBA{0, 0, 0, 0};
  ColorComponents c = colorComponentsFromColor(color);
  return GdkRGBA{c.red, c.green, c.blue, c.alpha};
}

bool is_paragraph(const ShadowView &view) {
  return std::strcmp(view.componentName, ParagraphComponentName) == 0;
}

}  // namespace

GtkMountingManager::GtkMountingManager(OnAfterMount onAfterMount)
    : onAfterMount_(std::move(onAfterMount)),
      mainThread_(std::this_thread::get_id()) {}

GtkMountingManager::~GtkMountingManager() noexcept {
  for (auto &[tag, widget] : views_) g_object_unref(widget);
}

void GtkMountingManager::registerSurface(SurfaceId surfaceId, GtkWidget *root) {
  roots_[surfaceId] = root;
  // The root view's tag is the surface id.
  views_[surfaceId] = GTK_WIDGET(g_object_ref(root));
}

void GtkMountingManager::unregisterSurface(SurfaceId surfaceId) {
  roots_.erase(surfaceId);
  if (auto it = views_.find(surfaceId); it != views_.end()) {
    g_object_unref(it->second);
    views_.erase(it);
  }
}

GtkWidget *GtkMountingManager::viewForTag(Tag tag) const {
  auto it = views_.find(tag);
  return it == views_.end() ? nullptr : it->second;
}

void GtkMountingManager::executeMount(SurfaceId surfaceId,
                                      MountingTransaction &&transaction) {
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (std::this_thread::get_id() != mainThread_ || !pending_.empty()) {
      pending_.emplace_back(surfaceId, std::move(transaction));
      if (!flushScheduled_) {
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
      return;
    }
  }
  apply(surfaceId, transaction);
}

void GtkMountingManager::flushPending() {
  for (;;) {
    std::unique_lock<std::mutex> lock(pendingMutex_);
    if (pending_.empty()) {
      flushScheduled_ = false;
      return;
    }
    auto [surfaceId, transaction] = std::move(pending_.front());
    pending_.pop_front();
    lock.unlock();
    apply(surfaceId, transaction);
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
        if (auto it = views_.find(m.oldChildShadowView.tag);
            it != views_.end()) {
          g_object_unref(it->second);
          views_.erase(it);
        }
        break;
      case ShadowViewMutation::Insert: {
        GtkWidget *parent = viewForTag(m.parentTag);
        GtkWidget *child = viewForTag(m.newChildShadowView.tag);
        if (!parent || !child || !RN_IS_VIEW(parent)) {
          LOG(ERROR) << "Insert: can't mount " << m.newChildShadowView.tag
                     << " into " << m.parentTag;
          break;
        }
        rn_view_insert_child(RN_VIEW(parent), child, m.index);
        applyLayout(child, m.newChildShadowView);
        break;
      }
      case ShadowViewMutation::Remove: {
        GtkWidget *parent = viewForTag(m.parentTag);
        GtkWidget *child = viewForTag(m.oldChildShadowView.tag);
        if (parent && child && gtk_widget_get_parent(child) == parent) {
          rn_view_remove_child(RN_VIEW(parent), child);
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
  mountCount_++;
  if (onAfterMount_) onAfterMount_(surfaceId);
}

void GtkMountingManager::create(const ShadowView &view) {
  GtkWidget *widget = is_paragraph(view) ? rn_text_new("") : rn_view_new();
  views_[view.tag] = GTK_WIDGET(g_object_ref_sink(widget));
  update(ShadowView{}, view);
}

void GtkMountingManager::update(const ShadowView &oldView,
                                const ShadowView &newView) {
  GtkWidget *widget = viewForTag(newView.tag);
  if (!widget) return;
  if (oldView.props != newView.props ||
      oldView.layoutMetrics != newView.layoutMetrics) {
    applyProps(widget, newView);
  }
  applyLayout(widget, newView);
  if (RN_IS_TEXT(widget)) applyParagraph(widget, newView);
}

void GtkMountingManager::applyProps(GtkWidget *widget, const ShadowView &view) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  if (!props) return;
  gtk_widget_set_opacity(widget, props->opacity);
  if (!RN_IS_VIEW(widget)) return;

  // GTK draws one radius, width and color per view for now; take the
  // top-left/left values when they differ per side.
  BorderMetrics border = props->resolveBorderMetrics(view.layoutMetrics);
  RNViewStyle style{};
  style.background = to_rgba(props->backgroundColor);
  style.border_radius = border.borderRadii.topLeft.horizontal;
  style.border_width = border.borderWidths.left;
  style.border_color = to_rgba(border.borderColors.left);
  style.clip_children = props->getClipsContentToBounds();
  rn_view_set_style(RN_VIEW(widget), &style);
}

void GtkMountingManager::applyLayout(GtkWidget *widget,
                                     const ShadowView &view) {
  const LayoutMetrics &lm = view.layoutMetrics;
  if (lm == EmptyLayoutMetrics) return;
  gtk_widget_set_visible(widget, lm.displayType != DisplayType::None);
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
  PangoLayout *layout = create_pango_layout(
      gtk_widget_get_pango_context(widget), data.attributedString,
      data.paragraphAttributes, view.layoutMetrics.frame.size.width);
  rn_text_set_layout(RN_TEXT(widget), layout);
  g_object_unref(layout);
}

void GtkMountingManager::dispatchCommand(const ShadowView &shadowView,
                                         const std::string &commandName,
                                         const folly::dynamic & /*args*/) {
  LOG(WARNING) << "Unsupported command " << commandName << " for "
               << shadowView.componentName;
}

ComponentRegistryFactory GtkMountingManager::getComponentRegistryFactory() {
  return [](const EventDispatcher::Weak &eventDispatcher,
            const std::shared_ptr<const ContextContainer> &contextContainer) {
    static auto providers = [] {
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
      return registry;
    }();
    return providers->createComponentDescriptorRegistry(
        {eventDispatcher, contextContainer, nullptr});
  };
}

}  // namespace rngtk
