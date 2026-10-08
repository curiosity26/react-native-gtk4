// Applies Fabric mount transactions to GTK widgets.
//
// Every host component becomes an RNView, except Paragraph, which becomes an
// RNText. Widgets are positioned at the frames Yoga computed.
//
// Transactions arrive on the JS thread (or ReactHost's reload thread); they
// are queued and applied on the GTK main thread, in order. Commands and
// native Animated's direct updates hop to the main thread the same way.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/componentregistry/ComponentDescriptorRegistry.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/imagemanager/ImageResponseObserverCoordinator.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/uimanager/IMountingManager.h>
#include <react/utils/ContextContainer.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace rngtk {

// Measures GTK controls the shadow nodes size to (the Switch); call on the
// main thread before JS starts.
void measure_native_controls();

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

  // ReactHost hands over each new instance's UIManager once the instance
  // is built; `callback` runs then (on the creating thread).
  void setUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) noexcept
      override;
  void setOnUIManagerChanged(std::function<void()> callback) {
    onUIManagerChanged_ = std::move(callback);
  }
  // No transactions or commands waiting for the main thread.
  bool isIdle();

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

  // Called when the user scrolls a scroll view: like a native scroller
  // taking over a gesture, in-flight touches are cancelled.
  void setOnUserScroll(std::function<void()> callback) {
    onUserScroll_ = std::move(callback);
  }
  // Loads Image's defaultSource (and backs ImageManager).
  void setImageLoader(std::shared_ptr<class GtkImageLoader> loader) {
    imageLoader_ = std::move(loader);
  }
  // How many onScroll events each scroll view sent (tests).
  int scrollEventCount(facebook::react::Tag tag) const;
  size_t mountedViewCount() const { return views_.size(); }
  int mountCount() const { return mountCount_; }

 private:
  void apply(facebook::react::SurfaceId surfaceId,
             const facebook::react::MountingTransaction &transaction);
  void flushPending();
  void scheduleFlushLocked();
  void create(const facebook::react::ShadowView &view);
  void update(const facebook::react::ShadowView &oldView,
              const facebook::react::ShadowView &newView);
  void applyProps(GtkWidget *widget, const facebook::react::ShadowView &view);

  // ScrollView (GtkScrollViews.cc)
  struct ScrollTracking {
    gint64 lastEventUs = 0;
    guint trailingEvent = 0;
    gint64 lastStateUs = 0;
    guint trailingState = 0;
    bool initialOffsetApplied = false;
    int events = 0;
  };
  void connectScrollView(GtkWidget *widget, facebook::react::Tag tag);
  void updateScrollView(GtkWidget *widget,
                        const facebook::react::ShadowView &oldView,
                        const facebook::react::ShadowView &newView);
  void onScrollOffsetChanged(facebook::react::Tag tag, bool user);
  void emitScrollEvent(facebook::react::Tag tag, const std::string &type);
  void updateScrollState(facebook::react::Tag tag);
  bool scrollCommand(GtkWidget *widget, const std::string &name,
                     const folly::dynamic &args);
  void forgetScrollView(facebook::react::Tag tag);
  static GtkWidget *containerFor(GtkWidget *parent);

  // TextInput (GtkTextInputs.cc)
  struct TextInputTracking {
    int nativeEventCount = 0;
    facebook::react::Size contentSize{};
    bool autoFocused = false;
  };
  void connectTextInput(GtkWidget *widget, facebook::react::Tag tag);
  void updateTextInput(GtkWidget *widget,
                       const facebook::react::ShadowView &oldView,
                       const facebook::react::ShadowView &newView);
  bool textInputCommand(GtkWidget *widget, facebook::react::Tag tag,
                        const std::string &name, const folly::dynamic &args);
  void onTextInputEvent(facebook::react::Tag tag, const std::string &type,
                        const std::string &arg);
  std::unordered_map<facebook::react::Tag, TextInputTracking> textInputs_;

  // Switch and ActivityIndicator (GtkControls.cc)
  void connectSwitch(GtkWidget *widget, facebook::react::Tag tag);
  void updateSwitch(GtkWidget *widget, const facebook::react::ShadowView &oldView,
                    const facebook::react::ShadowView &newView);
  bool switchCommand(GtkWidget *widget, const std::string &name,
                     const folly::dynamic &args);
  void updateSpinner(GtkWidget *widget,
                     const facebook::react::ShadowView &oldView,
                     const facebook::react::ShadowView &newView);

  // Image (GtkImages.cc)
  class ImageObserver;
  struct ImageTracking {
    std::shared_ptr<const facebook::react::ImageResponseObserverCoordinator>
        coordinator;
    std::shared_ptr<ImageObserver> observer;
    bool loaded = false;
  };
  void updateImage(GtkWidget *widget,
                   const facebook::react::ShadowView &oldView,
                   const facebook::react::ShadowView &newView);
  void imageLoaded(facebook::react::Tag tag, GdkTexture *texture);
  void imageFailed(facebook::react::Tag tag,
                   const facebook::react::ImageErrorInfo &error);
  void applyImage(facebook::react::Tag tag, GdkTexture *texture);
  void forgetImage(facebook::react::Tag tag);
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
  std::unordered_map<facebook::react::Tag, ScrollTracking> scrolls_;
  std::function<void()> onUserScroll_;
  std::function<void()> onUIManagerChanged_;
  bool onMainThread() const {
    return std::this_thread::get_id() == mainThread_;
  }
  // Runs `fn` on the main thread after the transactions queued before it.
  void runOnMainInOrder(std::function<void()> fn);
  std::unordered_map<facebook::react::Tag, ImageTracking> images_;
  std::shared_ptr<class GtkImageLoader> imageLoader_;
  int mountCount_{0};

  std::thread::id mainThread_;
  std::mutex pendingMutex_;
  // A transaction, or other work that must keep its place after them.
  struct PendingWork {
    facebook::react::SurfaceId surfaceId{};
    std::optional<facebook::react::MountingTransaction> transaction;
    std::function<void()> work;
  };
  std::deque<PendingWork> pending_;
  bool flushScheduled_{false};
};

}  // namespace rngtk
