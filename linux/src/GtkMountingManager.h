// Applies Fabric mount transactions to GTK widgets.
//
// Every host component becomes an RNView, except Paragraph, which becomes an
// RNText. Widgets are positioned at the frames Yoga computed.
//
// Transactions usually arrive on the main thread (the JS thread). Those
// that don't (a reload stops surfaces from ReactHost's reload thread) are
// queued and applied on the main thread, in order.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/componentregistry/ComponentDescriptorRegistry.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/uimanager/IMountingManager.h>
#include <react/utils/ContextContainer.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace rngtk {

class GtkMountingManager
    : public facebook::react::IMountingManager,
      public std::enable_shared_from_this<GtkMountingManager> {
 public:
  using OnAfterMount = std::function<void(facebook::react::SurfaceId)>;

  explicit GtkMountingManager(OnAfterMount onAfterMount);
  ~GtkMountingManager() noexcept override;

  // The widget a surface's root view mounts into. Owned by the caller; it
  // stays registered (and is never deleted) across stops and reloads.
  void registerSurface(facebook::react::SurfaceId surfaceId, GtkWidget *root);
  void unregisterSurface(facebook::react::SurfaceId surfaceId);

  void executeMount(
      facebook::react::SurfaceId surfaceId,
      facebook::react::MountingTransaction &&transaction) override;

  void dispatchCommand(
      const facebook::react::ShadowView &shadowView,
      const std::string &commandName,
      const folly::dynamic &args) override;

  facebook::react::ComponentRegistryFactory getComponentRegistryFactory()
      override;

  void synchronouslyUpdateViewOnUIThread(
      facebook::react::Tag tag, const folly::dynamic &props) override;

  GtkWidget *viewForTag(facebook::react::Tag tag) const;
  struct EventTarget {
    facebook::react::Tag tag = 0;
    facebook::react::SharedEventEmitter emitter;
  };
  // The React view a widget was mounted for. For a paragraph, pass a byte
  // index into its text to get the nested <Text> span there instead.
  EventTarget targetForView(GtkWidget *widget, int textIndex = -1) const;
  // Whether the mounted view's props ask for this event
  // (ViewEvents::Offset).
  bool hasEventListener(facebook::react::Tag tag, size_t offset) const;
  bool isSelectableText(facebook::react::Tag tag) const;
  // The mounted view with this nativeID, for tests.
  GtkWidget *viewForNativeId(const std::string &nativeId) const;
  size_t mountedViewCount() const { return views_.size(); }
  int mountCount() const { return mountCount_; }

 private:
  void apply(facebook::react::SurfaceId surfaceId,
             const facebook::react::MountingTransaction &transaction);
  void flushPending();
  void create(const facebook::react::ShadowView &view);
  void update(const facebook::react::ShadowView &oldView,
              const facebook::react::ShadowView &newView);
  void applyProps(GtkWidget *widget, const facebook::react::ShadowView &view);
  void forget(facebook::react::Tag tag);
  void applyLayout(GtkWidget *widget, const facebook::react::ShadowView &view);
  void applyParagraph(GtkWidget *widget,
                      const facebook::react::ShadowView &view);

  OnAfterMount onAfterMount_;
  // tag -> widget; we hold one reference to each.
  std::unordered_map<facebook::react::Tag, GtkWidget *> views_;
  std::unordered_map<facebook::react::SurfaceId, GtkWidget *> roots_;
  // The last mounted ShadowView per tag: props, layout, event emitter.
  std::unordered_map<facebook::react::Tag, facebook::react::ShadowView>
      shadowViews_;
  // Captured when the Scheduler builds its registry, to clone props for
  // native Animated's direct updates.
  std::weak_ptr<const facebook::react::ComponentDescriptorRegistry> registry_;
  std::shared_ptr<const facebook::react::ContextContainer> contextContainer_;
  int mountCount_{0};

  std::thread::id mainThread_;
  std::mutex pendingMutex_;
  std::deque<std::pair<facebook::react::SurfaceId,
                       facebook::react::MountingTransaction>>
      pending_;
  bool flushScheduled_{false};
};

}  // namespace rngtk
