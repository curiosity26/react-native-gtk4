// <Switch> (GtkSwitch) and <ActivityIndicator> (GtkSpinner) support in
// GtkMountingManager.
#include "GtkMountingManager.h"
#include "GtkSwitchShadowNode.h"
#include "GtkWindowControlsShadowNode.h"
#include "GtkViewProps.h"
#include "rn_css.h"

#include <react/renderer/components/FBReactNativeSpec/EventEmitters.h>
#include <react/renderer/components/FBReactNativeSpec/Props.h>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace facebook::react;

namespace rngtk {

namespace {

GQuark programmatic_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-programmatic");
  return q;
}

std::string css_rgba(const SharedColor &color) {
  GdkRGBA c = to_rgba(color);
  char buf[96];
  snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%.3f)", int(c.red * 255),
           int(c.green * 255), int(c.blue * 255), c.alpha);
  return buf;
}

// Per-widget CSS (track and thumb colors, spinner color); `&` is the
// widget.
void set_widget_css(GtkWidget *widget, const std::string &css) {
  rn_widget_set_css(widget, css.c_str());
}

void set_active(GtkWidget *widget, bool active) {
  if (bool(gtk_switch_get_active(GTK_SWITCH(widget))) == active) return;
  g_object_set_qdata(G_OBJECT(widget), programmatic_quark(), GINT_TO_POINTER(1));
  gtk_switch_set_active(GTK_SWITCH(widget), active);
  g_object_set_qdata(G_OBJECT(widget), programmatic_quark(), nullptr);
}

}  // namespace

void measure_native_controls() {
  // The theme's switch size, for the Switch shadow node (any thread).
  GtkWidget *sw = g_object_ref_sink(gtk_switch_new());
  int w, h, unused;
  gtk_widget_measure(sw, GTK_ORIENTATION_HORIZONTAL, -1, &unused, &w, nullptr, nullptr);
  gtk_widget_measure(sw, GTK_ORIENTATION_VERTICAL, w, &unused, &h, nullptr, nullptr);
  g_object_unref(sw);
  GtkSwitchShadowNode::setNativeSize(Size{.width = Float(w), .height = Float(h)});
  measure_window_controls();
}

bool measure_window_controls() {
  // In a window (never shown) like an app's: GtkWindowControls shows the
  // buttons its window allows (a resizable, sovereign one: all of them).
  static Size last[2] = {Size{.width = -1, .height = -1}, Size{.width = -1, .height = -1}};
  GtkWidget *window = gtk_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *controls[2] = {gtk_window_controls_new(GTK_PACK_START),
                            gtk_window_controls_new(GTK_PACK_END)};
  gtk_box_append(GTK_BOX(box), controls[0]);
  gtk_box_append(GTK_BOX(box), controls[1]);
  gtk_window_set_child(GTK_WINDOW(window), box);
  bool changed = false;
  for (int i = 0; i < 2; i++) {
    int w = 0, h = 0, unused;
    if (!gtk_window_controls_get_empty(GTK_WINDOW_CONTROLS(controls[i]))) {
      gtk_widget_measure(controls[i], GTK_ORIENTATION_HORIZONTAL, -1, &unused, &w, nullptr,
                         nullptr);
      gtk_widget_measure(controls[i], GTK_ORIENTATION_VERTICAL, w, &unused, &h, nullptr,
                         nullptr);
    }
    Size size{.width = Float(w), .height = Float(h)};
    changed |= size != last[i];
    last[i] = size;
    GtkWindowControlsShadowNode::setNativeSize(i == 0, size);
  }
  gtk_window_destroy(GTK_WINDOW(window));
  return changed;
}

void GtkMountingManager::connectSwitch(GtkWidget *widget, Tag tag) {
  struct Data {
    std::weak_ptr<GtkMountingManager> manager;
    Tag tag;
  };
  g_signal_connect_data(
      widget, "notify::active",
      G_CALLBACK(+[](GtkSwitch *sw, GParamSpec *, gpointer data) {
        auto *d = static_cast<Data *>(data);
        if (g_object_get_qdata(G_OBJECT(sw), programmatic_quark())) return;
        auto self = d->manager.lock();
        if (!self) return;
        auto view = self->shadowViews_.find(d->tag);
        if (view == self->shadowViews_.end()) return;
        if (auto emitter = std::dynamic_pointer_cast<const SwitchEventEmitter>(
                view->second.eventEmitter)) {
          // JS answers with the value it wants; a controlled Switch whose
          // value didn't change sends setValue to flip it back.
          emitter->onChange({.value = bool(gtk_switch_get_active(sw)), .target = d->tag});
        }
      }),
      new Data{weak_from_this(), tag},
      +[](gpointer d, GClosure *) { delete static_cast<Data *>(d); }, GConnectFlags(0));
}

void GtkMountingManager::updateSwitch(GtkWidget *widget, const ShadowView &oldView,
                                      const ShadowView &newView) {
  auto props = std::dynamic_pointer_cast<const SwitchProps>(newView.props);
  if (!props || oldView.props == newView.props) return;
  set_active(widget, props->value);
  gtk_widget_set_sensitive(widget, !props->disabled);
  // Switch.js sends trackColor as tintColor/onTintColor (off/on) and
  // thumbColor as thumbTintColor. GTK draws the track and the slider from
  // CSS backgrounds; the theme's borders and shadows stay.
  std::string css;
  SharedColor off = props->trackColorForFalse ? props->trackColorForFalse : props->tintColor;
  SharedColor on = props->trackColorForTrue ? props->trackColorForTrue : props->onTintColor;
  SharedColor thumb = props->thumbColor ? props->thumbColor : props->thumbTintColor;
  if (off) css += "&:not(:checked) { background-color: " + css_rgba(off) + "; background-image: none; }";
  if (on) css += "&:checked { background-color: " + css_rgba(on) + "; background-image: none; }";
  if (thumb) css += "& slider { background-color: " + css_rgba(thumb) + "; background-image: none; }";
  set_widget_css(widget, css);
}

bool GtkMountingManager::switchCommand(GtkWidget *widget, const std::string &name,
                                       const folly::dynamic &args) {
  if (name != "setValue") return false;
  if (args.isArray() && !args.empty() && args[0].isBool()) {
    set_active(widget, args[0].asBool());
  }
  return true;
}

void GtkMountingManager::updateSpinner(GtkWidget *widget, const ShadowView &oldView,
                                       const ShadowView &newView) {
  auto props = std::dynamic_pointer_cast<const ActivityIndicatorViewProps>(newView.props);
  if (!props) return;
  gtk_spinner_set_spinning(GTK_SPINNER(widget), props->animating);
  // applyLayout combines this with display: none.
  static GQuark hidden = g_quark_from_static_string("rngtk-hidden-by-props");
  bool hide = !props->animating && props->hidesWhenStopped;
  g_object_set_qdata(G_OBJECT(widget), hidden, GINT_TO_POINTER(hide ? 1 : 0));
  gtk_widget_set_visible(widget, !hide && newView.layoutMetrics.displayType !=
                                              DisplayType::None);
  if (oldView.props != newView.props ||
      oldView.layoutMetrics.frame.size != newView.layoutMetrics.frame.size) {
    // GTK's spinner icon draws in the current color (RN's default is
    // gray) at -gtk-icon-size: fill the frame.
    const auto &size = newView.layoutMetrics.frame.size;
    int icon = int(std::min(size.width, size.height));
    set_widget_css(widget, "& { color: " +
                               (props->color ? css_rgba(props->color)
                                             : std::string("rgba(153,153,153,1)")) +
                               "; -gtk-icon-size: " + std::to_string(std::max(icon, 1)) +
                               "px; min-width: 0; min-height: 0; }");
  }
}

}  // namespace rngtk
