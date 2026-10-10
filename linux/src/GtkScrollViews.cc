// ScrollView support in GtkMountingManager: RNScrollView widgets, their
// props and commands, and scroll events and state back to React.
#include "GtkMountingManager.h"

#include "rn_scroll_view.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>

using namespace facebook::react;

namespace rngtk {

namespace {

// Floor for scrollEventThrottle: about one event per frame.
constexpr gint64 kMinEventIntervalUs = 16000;
// Fabric's ScrollViewState (read by layout, measure and view culling) is
// updated less often while scrolling, and once more when it stops.
constexpr gint64 kStateIntervalUs = 100000;

struct SignalData {
  std::weak_ptr<GtkMountingManager> manager;
  Tag tag;
};

void free_signal_data(gpointer data, GClosure *) {
  delete static_cast<SignalData *>(data);
}

}  // namespace

GtkWidget *GtkMountingManager::containerFor(GtkWidget *parent) {
  if (parent && RN_IS_SCROLL_VIEW(parent)) {
    return rn_scroll_view_get_content(RN_SCROLL_VIEW(parent));
  }
  return parent;
}

int GtkMountingManager::scrollEventCount(Tag tag) const {
  auto it = scrolls_.find(tag);
  return it == scrolls_.end() ? 0 : it->second.events;
}

void GtkMountingManager::connectScrollView(GtkWidget *widget, Tag tag) {
  scrolls_[tag] = ScrollTracking{};
  auto connect = [&](const char *signal, GCallback callback) {
    g_signal_connect_data(widget, signal, callback,
                          new SignalData{weak_from_this(), tag},
                          free_signal_data, GConnectFlags(0));
  };
  connect("offset-changed",
          G_CALLBACK(+[](RNScrollView *, gboolean user, gpointer data) {
            auto *d = static_cast<SignalData *>(data);
            if (auto self = d->manager.lock()) {
              self->onScrollOffsetChanged(d->tag, user);
            }
          }));
  auto simple = [&](const char *signal, const char *type) {
    struct Data : SignalData {
      std::string type;
    };
    g_signal_connect_data(
        widget, signal,
        G_CALLBACK(+[](RNScrollView *, gpointer data) {
          auto *d = static_cast<Data *>(data);
          if (auto self = d->manager.lock()) self->emitScrollEvent(d->tag, d->type);
        }),
        new Data{{weak_from_this(), tag}, type},
        +[](gpointer data, GClosure *) { delete static_cast<Data *>(data); },
        GConnectFlags(0));
  };
  simple("drag-begin", "beginDrag");
  simple("drag-end", "endDrag");
  simple("momentum-begin", "momentumBegin");
  simple("momentum-end", "momentumEnd");
}

void GtkMountingManager::forgetScrollView(Tag tag) {
  auto it = scrolls_.find(tag);
  if (it == scrolls_.end()) return;
  if (it->second.trailingEvent) g_source_remove(it->second.trailingEvent);
  if (it->second.trailingState) g_source_remove(it->second.trailingState);
  scrolls_.erase(it);
}

void GtkMountingManager::updateScrollView(GtkWidget *widget,
                                          const ShadowView &oldView,
                                          const ShadowView &newView) {
  auto *scroll = RN_SCROLL_VIEW(widget);
  auto props = std::dynamic_pointer_cast<const ScrollViewProps>(newView.props);
  if (props && oldView.props != newView.props) {
    rn_scroll_view_set_horizontal(scroll, props->horizontal);
    rn_scroll_view_set_indicators(scroll, props->showsVerticalScrollIndicator,
                                  props->showsHorizontalScrollIndicator);
    rn_scroll_view_set_scroll_enabled(scroll, props->scrollEnabled);
  }
  // The content size comes from the shadow node, as on iOS.
  if (auto state =
          std::dynamic_pointer_cast<const ScrollViewShadowNode::ConcreteState>(
              newView.state)) {
    Size size = state->getData().getContentSize();
    rn_scroll_view_set_content_size(scroll, size.width, size.height);
  }
  auto &tracking = scrolls_[newView.tag];
  if (props && !tracking.initialOffsetApplied &&
      newView.layoutMetrics != EmptyLayoutMetrics) {
    tracking.initialOffsetApplied = true;
    Point offset = props->contentOffset;
    if (offset.x != 0 || offset.y != 0) {
      // After GTK has sized the content.
      struct Data {
        GtkWidget *widget;
        Point offset;
      };
      g_idle_add_full(
          G_PRIORITY_DEFAULT_IDLE,
          [](gpointer data) -> gboolean {
            auto *d = static_cast<Data *>(data);
            rn_scroll_view_scroll_to(RN_SCROLL_VIEW(d->widget), d->offset.x,
                                     d->offset.y, FALSE);
            return G_SOURCE_REMOVE;
          },
          new Data{GTK_WIDGET(g_object_ref(widget)), offset},
          [](gpointer data) {
            auto *d = static_cast<Data *>(data);
            g_object_unref(d->widget);
            delete d;
          });
    }
  }
}

bool GtkMountingManager::scrollCommand(GtkWidget *widget,
                                       const std::string &name,
                                       const folly::dynamic &args) {
  auto *scroll = RN_SCROLL_VIEW(widget);
  auto arg = [&](size_t i, double fallback) {
    return args.isArray() && args.size() > i && args[i].isNumber()
               ? args[i].asDouble()
               : fallback;
  };
  auto flag = [&](size_t i) {
    return args.isArray() && args.size() > i && args[i].isBool() &&
           args[i].asBool();
  };
  if (name == "scrollTo") {
    rn_scroll_view_scroll_to(scroll, arg(0, 0), arg(1, 0), flag(2));
    return true;
  }
  if (name == "scrollToEnd") {
    double x, y;
    rn_scroll_view_get_offset(scroll, &x, &y);
    auto tag = GPOINTER_TO_INT(
        g_object_get_qdata(G_OBJECT(widget), g_quark_from_static_string("rn-tag")));
    auto it = shadowViews_.find(tag);
    auto props = it == shadowViews_.end()
                     ? nullptr
                     : std::dynamic_pointer_cast<const ScrollViewProps>(
                           it->second.props);
    // Clamped to the end of the content.
    if (props && props->horizontal) {
      x = 1e9;
    } else {
      y = 1e9;
    }
    rn_scroll_view_scroll_to(scroll, x, y, flag(0));
    return true;
  }
  if (name == "flashScrollIndicators" || name == "zoomToRect" ||
      name == "scrollToOverflowEnabled") {
    return true;  // GTK's overlay scrollbars show on motion; no zoom.
  }
  return false;
}

void GtkMountingManager::onScrollOffsetChanged(Tag tag, bool user) {
  if (user && onUserScroll_) onUserScroll_();
  if (user && !scrollObservers_.empty()) {
    if (GtkWidget *view = viewForTag(tag)) {
      for (auto &observer : scrollObservers_) observer(view);
    }
  }
  auto it = scrolls_.find(tag);
  if (it == scrolls_.end()) return;
  auto &tracking = it->second;
  gint64 now = g_get_monotonic_time();

  // onScroll, at most once per scrollEventThrottle (and per frame), with a
  // trailing event so the last offset always reaches JS.
  gint64 interval = kMinEventIntervalUs;
  if (auto view = shadowViews_.find(tag); view != shadowViews_.end()) {
    if (auto props = std::dynamic_pointer_cast<const ScrollViewProps>(
            view->second.props)) {
      interval = std::max(interval, gint64(props->scrollEventThrottle * 1000));
    }
  }
  auto weak = weak_from_this();
  auto schedule = [&](guint &source, gint64 delayUs, bool state) {
    if (source) return;
    struct Data {
      std::weak_ptr<GtkMountingManager> manager;
      Tag tag;
      bool state;
    };
    source = g_timeout_add_full(
        G_PRIORITY_DEFAULT, guint(std::max<gint64>(1, delayUs / 1000)),
        [](gpointer data) -> gboolean {
          auto *d = static_cast<Data *>(data);
          if (auto self = d->manager.lock()) {
            auto it = self->scrolls_.find(d->tag);
            if (it != self->scrolls_.end()) {
              if (d->state) {
                it->second.trailingState = 0;
                self->updateScrollState(d->tag);
              } else {
                it->second.trailingEvent = 0;
                self->emitScrollEvent(d->tag, "scroll");
              }
            }
          }
          return G_SOURCE_REMOVE;
        },
        new Data{weak, tag, state},
        [](gpointer data) { delete static_cast<Data *>(data); });
  };
  if (now - tracking.lastEventUs >= interval && !tracking.trailingEvent) {
    emitScrollEvent(tag, "scroll");
  } else {
    schedule(tracking.trailingEvent, interval - (now - tracking.lastEventUs),
             false);
  }
  if (now - tracking.lastStateUs >= kStateIntervalUs && !tracking.trailingState) {
    updateScrollState(tag);
  } else {
    schedule(tracking.trailingState,
             kStateIntervalUs - (now - tracking.lastStateUs), true);
  }
}

void GtkMountingManager::emitScrollEvent(Tag tag, const std::string &type) {
  GtkWidget *widget = viewForTag(tag);
  auto view = shadowViews_.find(tag);
  if (!widget || !RN_IS_SCROLL_VIEW(widget) || view == shadowViews_.end()) {
    return;
  }
  auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(
      view->second.eventEmitter);
  if (!emitter) return;
  auto *scroll = RN_SCROLL_VIEW(widget);

  ScrollEvent event;
  double x, y, w, h;
  rn_scroll_view_get_offset(scroll, &x, &y);
  rn_scroll_view_get_viewport_size(scroll, &w, &h);
  graphene_rect_t content =
      rn_widget_get_frame(rn_scroll_view_get_content(scroll));
  event.contentOffset = {.x = Float(x), .y = Float(y)};
  event.contentSize = {.width = content.size.width,
                       .height = content.size.height};
  event.containerSize = {.width = Float(w), .height = Float(h)};
  event.zoomScale = 1;
  event.timestamp = Float(g_get_monotonic_time() / 1e6);

  if (type == "scroll") {
    auto &tracking = scrolls_[tag];
    tracking.lastEventUs = g_get_monotonic_time();
    tracking.events++;
    emitter->onScroll(event);
  } else if (type == "beginDrag") {
    emitter->onScrollBeginDrag(event);
  } else if (type == "endDrag") {
    ScrollEndDragEvent end(event);
    end.targetContentOffset = event.contentOffset;
    emitter->onScrollEndDrag(end);
  } else if (type == "momentumBegin") {
    emitter->onMomentumScrollBegin(event);
  } else if (type == "momentumEnd") {
    emitter->onMomentumScrollEnd(event);
    updateScrollState(tag);
  }
}

void GtkMountingManager::updateScrollState(Tag tag) {
  GtkWidget *widget = viewForTag(tag);
  auto view = shadowViews_.find(tag);
  if (!widget || !RN_IS_SCROLL_VIEW(widget) || view == shadowViews_.end()) {
    return;
  }
  auto state = std::dynamic_pointer_cast<const ScrollViewShadowNode::ConcreteState>(
      view->second.state);
  if (!state) return;
  scrolls_[tag].lastStateUs = g_get_monotonic_time();
  double x, y;
  rn_scroll_view_get_offset(RN_SCROLL_VIEW(widget), &x, &y);
  ScrollViewState data = state->getData();
  if (data.contentOffset.x == Float(x) && data.contentOffset.y == Float(y)) {
    return;
  }
  data.contentOffset = {.x = Float(x), .y = Float(y)};
  // Commits through the UIManager, like iOS's _updateStateWithContentOffset.
  state->updateState(std::move(data));
}

}  // namespace rngtk
