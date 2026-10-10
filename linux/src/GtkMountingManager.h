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
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/componentregistry/ComponentDescriptorRegistry.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/imagemanager/ImageResponseObserverCoordinator.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/uimanager/IMountingManager.h>
#include <react/utils/ContextContainer.h>
#include <rngtk/Extensions.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rngtk {

// Measures GTK controls the shadow nodes size to (the Switch, the window
// controls); call on the main thread before JS starts.
void measure_native_controls();
// Measures the window controls again (the decoration layout or the theme
// changed); true if their size changed.
bool measure_window_controls();

class GtkMountingManager
    : public facebook::react::IMountingManager,
      public std::enable_shared_from_this<GtkMountingManager> {
 public:
  using OnAfterMount = std::function<void(facebook::react::SurfaceId)>;

  explicit GtkMountingManager(OnAfterMount onAfterMount);
  ~GtkMountingManager() noexcept override;

  // Libraries' components (rngtk/Extensions.h); before JS starts.
  void addNativeComponents(const std::vector<NativeComponent> &components);

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
  // Main thread: runs `fn` once the transactions queued so far are applied
  // (now, if none are).
  void afterPendingMounts(std::function<void()> fn) { runOnMainInOrder(std::move(fn)); }
  // Main thread: has screen readers speak `text` (an AccessibilityInfo
  // announcement or a live region's change); the last one, for tests.
  void announce(GtkWidget *from, const std::string &text,
                GtkAccessibleAnnouncementPriority priority);
  const std::string &lastAnnouncement() const { return lastAnnouncement_; }
  // While a screen reader runs, accessible elements that aren't focusable
  // (an `accessible` View, a Text) take focus too, so Tab reaches them
  // like VoiceOver and TalkBack do: Orca reads what has focus.
  void setScreenReaderActive(bool active);
  // GNOME's text scaling, for TextInput's font (Text gets it through
  // the layout context's fontSizeMultiplier).
  void setFontScale(float scale);
  // Main thread: re-applies every mounted view's colors, after the light
  // or dark palette PlatformColors resolve to changed.
  void refreshColors();

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
  // Right-click, the Menu key or Shift+F10 on `widget` (under `root`, a
  // surface's or a modal's root): pops up the contextMenu of the nearest
  // view that has one, at (x, y) in root coordinates, or at the view when
  // x < 0. A TextInput or selectable Text keeps its own menu unless it has
  // a contextMenu itself. False if nothing popped up.
  bool showContextMenu(GtkWidget *root, GtkWidget *widget, double x, double y);
  // The context menu showing now (tests), or null.
  GtkWidget *contextMenuPopover() const {
    return contextMenu_ && gtk_widget_get_visible(contextMenu_) ? contextMenu_ : nullptr;
  }
  // The mounted view's props, or null.
  facebook::react::Props::Shared propsForTag(facebook::react::Tag tag) const;
  // The mounted view with this nativeID, for tests.
  GtkWidget *viewForNativeId(const std::string &nativeId) const;

  // The mounted view with this accessibilityLabel, for tests.
  GtkWidget *viewForAccessibilityLabel(const std::string &label) const;

  // Libraries' pointer observers (rngtk::Host::addPointerObserver); true
  // if one consumed the event.
  void addPointerObserver(std::function<bool(const PointerInput &)> observer) {
    pointerObservers_.push_back(std::move(observer));
  }
  bool observePointer(const PointerInput &event) {
    bool consumed = false;
    for (auto &observer : pointerObservers_) consumed = observer(event) || consumed;
    return consumed;
  }
  bool hasPointerObservers() const { return !pointerObservers_.empty(); }
  void addScrollObserver(std::function<void(GtkWidget *)> observer) {
    scrollObservers_.push_back(std::move(observer));
  }
  // The mounted view's component name, or "".
  std::string componentNameForTag(facebook::react::Tag tag) const;

  // Back navigation the user asked for (Alt+Left, the Back key, the
  // mouse's back button): BackHandler's hardwareBackPress, as on
  // react-native-windows.
  void setOnBackRequested(std::function<void()> callback) {
    onBackRequested_ = std::move(callback);
  }
  void requestBack() {
    if (onBackRequested_) onBackRequested_();
  }

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

  // Modals (GtkModals.cc): the windows of the <Modal>s mounted now, by
  // their ModalHostView's tag, and their input handlers (tests).
  std::vector<facebook::react::Tag> modalTags() const;
  GtkWindow *modalWindow(facebook::react::Tag tag) const;
  class GtkPointerHandler *modalPointerHandler(facebook::react::Tag tag) const;
  class GtkKeyboardHandler *modalKeyboardHandler(facebook::react::Tag tag) const;
  // Closes every modal window (shutting down).
  void closeAllModals();

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
  // Modal (GtkModals.cc)
  struct ModalWindow;
  static bool isModalHost(const facebook::react::ShadowView &view);
  bool isModalTag(facebook::react::Tag tag) const;
  void mountModal(facebook::react::Tag parentTag, facebook::react::Tag tag);
  void presentPendingModals();
  void openModal(GtkWindow *parentWindow, facebook::react::Tag tag,
                 const facebook::react::ShadowView &view);
  void setModalSize(facebook::react::Tag tag, float width, float height);
  void followModalSize(facebook::react::Tag tag);
  void presentModal(facebook::react::Tag tag);
  void dismissModal(facebook::react::Tag tag);
  void startModalAnimation(facebook::react::Tag tag);
  bool stepModalAnimation(facebook::react::Tag tag, gint64 now);
  void requestModalClose(facebook::react::Tag tag);
  void updateModal(const facebook::react::ShadowView &oldView,
                   const facebook::react::ShadowView &newView);
  void layoutModal(facebook::react::Tag tag);
  void unmountModal(facebook::react::Tag tag);
  // shared_ptr: ModalWindow is complete only in GtkModals.cc.
  std::unordered_map<facebook::react::Tag, std::shared_ptr<ModalWindow>> modals_;
  // Libraries' components, by component name.
  std::unordered_map<std::string, std::shared_ptr<const NativeComponent>> nativeComponents_;
  std::shared_ptr<facebook::react::ComponentDescriptorProviderRegistry> providers_;
  const NativeComponent *nativeComponentFor(const facebook::react::ShadowView &view) const;
  // The library component a mounted tag is, if it's one.
  const NativeComponent *nativeComponentForTag(facebook::react::Tag tag) const;
  // (parent, modal) inserted in the transaction being applied.
  std::vector<std::pair<facebook::react::Tag, facebook::react::Tag>> pendingModals_;
  // Accessibility (GtkAccessibility.cc)
  // A view screen readers should see a value for (accessibilityValue, or
  // a range role): it mounts as an RNRangeView.
  static bool hasAccessibleValue(const facebook::react::ShadowView &view);
  static GtkAccessibleRole accessibleRoleFor(
      const facebook::react::ShadowView &view, GtkWidget *widget);
  void updateAccessibility(GtkWidget *widget,
                           const facebook::react::ShadowView &oldView,
                           const facebook::react::ShadowView &newView);
  void updateAccessibilityActions(GtkWidget *widget,
                                  const facebook::react::ShadowView &view);
  void updateTextAccessibility(GtkWidget *widget,
                               const facebook::react::ShadowView &view,
                               const std::string &text);
  void refreshContentLabels(GtkWidget *from);
  void updateScreenReaderFocus(GtkWidget *widget);
  // View: autoFocus, and the focus/blur commands (ref.focus()).
  void updateFocus(GtkWidget *widget, const facebook::react::ShadowView &view);
  bool focusCommand(GtkWidget *widget, const std::string &name);
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
  std::function<void()> onBackRequested_;
  std::vector<std::function<bool(const PointerInput &)>> pointerObservers_;
  std::vector<std::function<void(GtkWidget *)>> scrollObservers_;
  std::function<void()> onUIManagerChanged_;
  bool onMainThread() const {
    return std::this_thread::get_id() == mainThread_;
  }
  // Runs `fn` on the main thread after the transactions queued before it.
  void runOnMainInOrder(std::function<void()> fn);
  std::unordered_map<facebook::react::Tag, ImageTracking> images_;
  std::shared_ptr<class GtkImageLoader> imageLoader_;
  int mountCount_{0};
  std::string lastAnnouncement_;
  GtkWidget *contextMenu_{nullptr};  // weak
  float fontScale_{1};
  bool screenReaderActive_{false};

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
