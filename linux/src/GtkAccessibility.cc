// Accessibility in GtkMountingManager: React Native's accessibility props
// on the widgets' GtkAccessible, which GTK exposes over AT-SPI (Orca,
// Accerciser).
//
// - Role: `role` or `accessibilityRole`, mapped to GtkAccessibleRole and set
//   before the widget is realized (GTK can't change it after). Text is a
//   label; an `accessible` View with no role a group; other Views generic.
// - Name: accessibilityLabel (aria-label); a Text's own text; an
//   `accessible` group's descendant text, like iOS (GTK computes buttons'
//   and links' names from their content itself). accessibilityLabelledBy
//   (aria-labelledby) as a labelled-by relation to those nativeIDs.
// - Description: accessibilityHint.
// - States: accessibilityState (and the aria-* props, which View.js folds
//   into it): disabled, selected, checked (or pressed, for a togglebutton),
//   busy, expanded; hidden, with its whole subtree (the view lists no
//   accessible children), for
//   accessibilityElementsHidden, aria-hidden and importantForAccessibility
//   'no-hide-descendants'.
// - Value: accessibilityValue min, max, now and text (AT-SPI's Value
//   interface: RNView is a GtkAccessibleRange). A screen reader setting
//   the value sends onAccessibilityAction increment or decrement.
// - Actions: accessibilityActions become actions on the widget ("a11y."
//   plus the name), which GTK lists in AT-SPI's Action interface;
//   performing one sends onAccessibilityAction. A view that listens for
//   presses also gets "a11y.activate", which presses it like Enter does.
// - Live regions: when a Text's content changes inside a view with
//   accessibilityLiveRegion, the new text is announced.
// - Screen reader navigation: Orca reads what has keyboard focus, so while
//   a screen reader runs, accessible elements that don't take focus (an
//   `accessible` View, a Text outside one) do: Tab visits each, as
//   VoiceOver and TalkBack swipes do. Off, the Tab order is unchanged.
#include "GtkMountingManager.h"

#include "rn_text.h"
#include "rn_text_input.h"
#include "rn_view.h"

#include <react/renderer/components/view/BaseViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/accessibilityPropsConversions.h>
#include <react/renderer/components/view/primitives.h>

#include <cstring>
#include <unordered_map>

using namespace facebook::react;

namespace rngtk {

namespace {

GtkAccessibleRole roleForName(const std::string &name) {
  static const std::unordered_map<std::string, GtkAccessibleRole> roles = {
      {"alert", GTK_ACCESSIBLE_ROLE_ALERT},
      {"alertdialog", GTK_ACCESSIBLE_ROLE_ALERT_DIALOG},
      {"application", GTK_ACCESSIBLE_ROLE_APPLICATION},
      {"article", GTK_ACCESSIBLE_ROLE_ARTICLE},
      {"banner", GTK_ACCESSIBLE_ROLE_BANNER},
      {"button", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"imagebutton", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"keyboardkey", GTK_ACCESSIBLE_ROLE_BUTTON},
      {"togglebutton", GTK_ACCESSIBLE_ROLE_TOGGLE_BUTTON},
      {"cell", GTK_ACCESSIBLE_ROLE_CELL},
      {"checkbox", GTK_ACCESSIBLE_ROLE_CHECKBOX},
      {"columnheader", GTK_ACCESSIBLE_ROLE_COLUMN_HEADER},
      {"combobox", GTK_ACCESSIBLE_ROLE_COMBO_BOX},
      {"dropdownlist", GTK_ACCESSIBLE_ROLE_COMBO_BOX},
      {"complementary", GTK_ACCESSIBLE_ROLE_LANDMARK},
      {"contentinfo", GTK_ACCESSIBLE_ROLE_LANDMARK},
      {"dialog", GTK_ACCESSIBLE_ROLE_DIALOG},
      {"document", GTK_ACCESSIBLE_ROLE_DOCUMENT},
      {"webview", GTK_ACCESSIBLE_ROLE_DOCUMENT},
      {"feed", GTK_ACCESSIBLE_ROLE_FEED},
      {"form", GTK_ACCESSIBLE_ROLE_FORM},
      {"grid", GTK_ACCESSIBLE_ROLE_GRID},
      {"group", GTK_ACCESSIBLE_ROLE_GROUP},
      {"radiogroup", GTK_ACCESSIBLE_ROLE_RADIO_GROUP},
      {"heading", GTK_ACCESSIBLE_ROLE_HEADING},
      {"header", GTK_ACCESSIBLE_ROLE_HEADING},
      {"img", GTK_ACCESSIBLE_ROLE_IMG},
      {"image", GTK_ACCESSIBLE_ROLE_IMG},
      {"figure", GTK_ACCESSIBLE_ROLE_IMG},
      {"link", GTK_ACCESSIBLE_ROLE_LINK},
      {"list", GTK_ACCESSIBLE_ROLE_LIST},
      {"listitem", GTK_ACCESSIBLE_ROLE_LIST_ITEM},
      {"log", GTK_ACCESSIBLE_ROLE_LOG},
      {"main", GTK_ACCESSIBLE_ROLE_MAIN},
      {"marquee", GTK_ACCESSIBLE_ROLE_MARQUEE},
      {"math", GTK_ACCESSIBLE_ROLE_MATH},
      {"menu", GTK_ACCESSIBLE_ROLE_MENU},
      {"iconmenu", GTK_ACCESSIBLE_ROLE_MENU},
      {"menubar", GTK_ACCESSIBLE_ROLE_MENU_BAR},
      {"menuitem", GTK_ACCESSIBLE_ROLE_MENU_ITEM},
      {"meter", GTK_ACCESSIBLE_ROLE_METER},
      {"navigation", GTK_ACCESSIBLE_ROLE_NAVIGATION},
      {"note", GTK_ACCESSIBLE_ROLE_NOTE},
      {"option", GTK_ACCESSIBLE_ROLE_OPTION},
      {"presentation", GTK_ACCESSIBLE_ROLE_PRESENTATION},
      {"progressbar", GTK_ACCESSIBLE_ROLE_PROGRESS_BAR},
      {"radio", GTK_ACCESSIBLE_ROLE_RADIO},
      {"region", GTK_ACCESSIBLE_ROLE_REGION},
      {"row", GTK_ACCESSIBLE_ROLE_ROW},
      {"rowgroup", GTK_ACCESSIBLE_ROLE_ROW_GROUP},
      {"rowheader", GTK_ACCESSIBLE_ROLE_ROW_HEADER},
      {"scrollbar", GTK_ACCESSIBLE_ROLE_SCROLLBAR},
      // RN's "search" is a search field (iOS's search trait).
      {"search", GTK_ACCESSIBLE_ROLE_SEARCH_BOX},
      {"searchbox", GTK_ACCESSIBLE_ROLE_SEARCH_BOX},
      {"separator", GTK_ACCESSIBLE_ROLE_SEPARATOR},
      {"adjustable", GTK_ACCESSIBLE_ROLE_SLIDER},
      {"slider", GTK_ACCESSIBLE_ROLE_SLIDER},
      {"spinbutton", GTK_ACCESSIBLE_ROLE_SPIN_BUTTON},
      {"status", GTK_ACCESSIBLE_ROLE_STATUS},
      {"summary", GTK_ACCESSIBLE_ROLE_GROUP},
      {"switch", GTK_ACCESSIBLE_ROLE_SWITCH},
      {"tab", GTK_ACCESSIBLE_ROLE_TAB},
      {"table", GTK_ACCESSIBLE_ROLE_TABLE},
      {"tablist", GTK_ACCESSIBLE_ROLE_TAB_LIST},
      {"tabbar", GTK_ACCESSIBLE_ROLE_TAB_LIST},
      {"tabpanel", GTK_ACCESSIBLE_ROLE_TAB_PANEL},
      {"text", GTK_ACCESSIBLE_ROLE_LABEL},
      {"timer", GTK_ACCESSIBLE_ROLE_TIMER},
      {"toolbar", GTK_ACCESSIBLE_ROLE_TOOLBAR},
      {"tooltip", GTK_ACCESSIBLE_ROLE_TOOLTIP},
      {"tree", GTK_ACCESSIBLE_ROLE_TREE},
      {"treegrid", GTK_ACCESSIBLE_ROLE_TREE_GRID},
      {"treeitem", GTK_ACCESSIBLE_ROLE_TREE_ITEM},
      {"directory", GTK_ACCESSIBLE_ROLE_LIST},
      {"definition", GTK_ACCESSIBLE_ROLE_PARAGRAPH},
      {"term", GTK_ACCESSIBLE_ROLE_LABEL},
      {"scrollview", GTK_ACCESSIBLE_ROLE_GROUP},
      {"horizontalscrollview", GTK_ACCESSIBLE_ROLE_GROUP},
      {"viewgroup", GTK_ACCESSIBLE_ROLE_GROUP},
      {"pager", GTK_ACCESSIBLE_ROLE_GROUP},
  };
  auto it = roles.find(name);
  return it == roles.end() ? GTK_ACCESSIBLE_ROLE_NONE : it->second;
}

GQuark touched_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-a11y-touched");
  return q;
}
// An `accessible` view named by its descendants' text.
GQuark content_label_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-a11y-content-label");
  return q;
}
GQuark actions_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-a11y-actions");
  return q;
}
GQuark range_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-a11y-range");
  return q;
}

GQuark text_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-a11y-text");
  return q;
}

bool has_accessibility_props(const ViewProps &p) {
  return p.accessible || !p.accessibilityLabel.empty() ||
         !p.accessibilityHint.empty() || p.accessibilityState.has_value() ||
         !p.accessibilityRole.empty() || p.role != Role::None ||
         p.accessibilityValue.min || p.accessibilityValue.max ||
         p.accessibilityValue.now || p.accessibilityValue.text ||
         !p.accessibilityLabelledBy.value.empty() ||
         !p.accessibilityActions.empty() || p.accessibilityElementsHidden ||
         p.accessibilityViewIsModal ||
         p.importantForAccessibility != ImportantForAccessibility::Auto ||
         p.accessibilityLiveRegion != AccessibilityLiveRegion::None;
}

void set_label(GtkAccessible *acc, const std::string &label) {
  if (label.empty()) {
    gtk_accessible_reset_property(acc, GTK_ACCESSIBLE_PROPERTY_LABEL);
  } else {
    gtk_accessible_update_property(acc, GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   label.c_str(), -1);
  }
}

// The text of RNTexts under `widget`, in order, space-separated.
void collect_text(GtkWidget *widget, std::string &out) {
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    if (!gtk_widget_get_visible(c) || GTK_IS_NATIVE(c)) continue;
    if (RN_IS_TEXT(c)) {
      const char *text = rn_text_get_text(RN_TEXT(c));
      if (text && *text) {
        if (!out.empty()) out += ' ';
        out += text;
      }
    } else {
      collect_text(c, out);
    }
  }
}

}  // namespace

bool GtkMountingManager::hasAccessibleValue(const ShadowView &view) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  if (!props || std::strcmp(view.componentName, "View") != 0) return false;
  const auto &v = props->accessibilityValue;
  if (v.min || v.max || v.now) return true;
  switch (accessibleRoleFor(view, nullptr)) {
    case GTK_ACCESSIBLE_ROLE_SLIDER:
    case GTK_ACCESSIBLE_ROLE_PROGRESS_BAR:
    case GTK_ACCESSIBLE_ROLE_SPIN_BUTTON:
    case GTK_ACCESSIBLE_ROLE_SCROLLBAR:
    case GTK_ACCESSIBLE_ROLE_METER:
      return true;
    default:
      return false;
  }
}

GtkAccessibleRole GtkMountingManager::accessibleRoleFor(const ShadowView &view,
                                                        GtkWidget *widget) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  bool text = widget && RN_IS_TEXT(widget);
  if (!props) return text ? GTK_ACCESSIBLE_ROLE_LABEL : GTK_ACCESSIBLE_ROLE_GENERIC;
  if (props->importantForAccessibility == ImportantForAccessibility::No) {
    return GTK_ACCESSIBLE_ROLE_PRESENTATION;
  }
  GtkAccessibleRole role = GTK_ACCESSIBLE_ROLE_NONE;
  if (props->role != Role::None) role = roleForName(toString(props->role));
  if (role == GTK_ACCESSIBLE_ROLE_NONE && !props->accessibilityRole.empty()) {
    role = roleForName(props->accessibilityRole);
  }
  if (role != GTK_ACCESSIBLE_ROLE_NONE) return role;
  if (text) return GTK_ACCESSIBLE_ROLE_LABEL;
  // A labelled or `accessible` view is one element: a (named) group.
  return props->accessible || !props->accessibilityLabel.empty()
             ? GTK_ACCESSIBLE_ROLE_GROUP
             : GTK_ACCESSIBLE_ROLE_GENERIC;
}

void GtkMountingManager::updateAccessibility(GtkWidget *widget,
                                             const ShadowView &oldView,
                                             const ShadowView &view) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  if (!props || oldView.props == view.props) return;
  bool ours = RN_IS_VIEW(widget) || RN_IS_TEXT(widget);
  // GTK's controls keep their own role and states: TextInput's editor and
  // Switch get the name, description and hidden state.
  GtkWidget *target = widget;
  if (RN_IS_TEXT_INPUT(widget)) {
    target = rn_text_input_get_editor(RN_TEXT_INPUT(widget));
    if (!target) return;
  }
  auto *acc = GTK_ACCESSIBLE(target);

  if (ours) {
    // Only until GTK realizes the widget's accessible.
    GtkAccessibleRole role = accessibleRoleFor(view, widget);
    if (role != gtk_accessible_get_accessible_role(acc) &&
        !gtk_widget_get_realized(widget)) {
      g_object_set(widget, "accessible-role", role, nullptr);
    }
  }

  if (!has_accessibility_props(*props) &&
      !g_object_get_qdata(G_OBJECT(widget), touched_quark())) {
    return;  // nothing set before, nothing to set (most views)
  }
  g_object_set_qdata(G_OBJECT(widget), touched_quark(), GINT_TO_POINTER(1));

  // Name. A Text's comes from applyParagraph (its text, unless labelled).
  bool content_label = ours && !RN_IS_TEXT(widget) && props->accessible &&
                       props->accessibilityLabel.empty() &&
                       props->role == Role::None &&
                       props->accessibilityRole.empty();
  g_object_set_qdata(G_OBJECT(widget), content_label_quark(),
                     GINT_TO_POINTER(content_label ? 1 : 0));
  if (content_label) {
    refreshContentLabels(widget);
  } else if (!RN_IS_TEXT(widget)) {
    set_label(acc, props->accessibilityLabel);
  } else if (!props->accessibilityLabel.empty()) {
    set_label(acc, props->accessibilityLabel);
  }

  if (props->accessibilityHint.empty()) {
    gtk_accessible_reset_property(acc, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION);
  } else {
    gtk_accessible_update_property(acc, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                   props->accessibilityHint.c_str(), -1);
  }

  // Labelled by other views (nativeIDs).
  GList *labels = nullptr;
  for (const auto &id : props->accessibilityLabelledBy.value) {
    if (GtkWidget *w = viewForNativeId(id)) {
      labels = g_list_append(labels, w);
    }
  }
  if (labels) {
    gtk_accessible_update_relation(acc, GTK_ACCESSIBLE_RELATION_LABELLED_BY,
                                   labels, -1);
    // GTK 4.14 doesn't name an element from the relation: the labelling
    // views' text becomes its name too.
    if (props->accessibilityLabel.empty()) {
      std::string name;
      for (GList *l = labels; l; l = l->next) {
        auto *w = GTK_WIDGET(l->data);
        std::string text;
        if (RN_IS_TEXT(w)) {
          text = rn_text_get_text(RN_TEXT(w));
        } else {
          collect_text(w, text);
        }
        if (!text.empty()) name += (name.empty() ? "" : " ") + text;
      }
      set_label(acc, name);
    }
    g_list_free(labels);
  } else {
    gtk_accessible_reset_relation(acc, GTK_ACCESSIBLE_RELATION_LABELLED_BY);
  }

  bool hidden = props->accessibilityElementsHidden ||
                props->importantForAccessibility ==
                    ImportantForAccessibility::NoHideDescendants;
  if (RN_IS_VIEW(widget)) {
    rn_view_set_accessible_hidden(RN_VIEW(widget), hidden);
  } else if (hidden) {
    gtk_accessible_update_state(acc, GTK_ACCESSIBLE_STATE_HIDDEN, TRUE, -1);
  } else {
    gtk_accessible_reset_state(acc, GTK_ACCESSIBLE_STATE_HIDDEN);
  }
  if (props->accessibilityViewIsModal) {
    gtk_accessible_update_property(acc, GTK_ACCESSIBLE_PROPERTY_MODAL, TRUE, -1);
  } else {
    gtk_accessible_reset_property(acc, GTK_ACCESSIBLE_PROPERTY_MODAL);
  }
  if (!ours) return;

  // States.
  const AccessibilityState state =
      props->accessibilityState.value_or(AccessibilityState{});
  gtk_accessible_update_state(acc, GTK_ACCESSIBLE_STATE_DISABLED, state.disabled,
                              GTK_ACCESSIBLE_STATE_BUSY, state.busy, -1);
  if (props->accessibilityState && state.selected) {
    gtk_accessible_update_state(acc, GTK_ACCESSIBLE_STATE_SELECTED, TRUE, -1);
  } else {
    gtk_accessible_reset_state(acc, GTK_ACCESSIBLE_STATE_SELECTED);
  }
  if (state.expanded) {
    gtk_accessible_update_state(acc, GTK_ACCESSIBLE_STATE_EXPANDED,
                                *state.expanded, -1);
  } else {
    gtk_accessible_reset_state(acc, GTK_ACCESSIBLE_STATE_EXPANDED);
  }
  bool toggle = gtk_accessible_get_accessible_role(acc) ==
                GTK_ACCESSIBLE_ROLE_TOGGLE_BUTTON;
  GtkAccessibleState checked =
      toggle ? GTK_ACCESSIBLE_STATE_PRESSED : GTK_ACCESSIBLE_STATE_CHECKED;
  if (state.checked == AccessibilityState::None) {
    gtk_accessible_reset_state(acc, checked);
  } else {
    gtk_accessible_update_state(
        acc, checked,
        state.checked == AccessibilityState::Checked ? GTK_ACCESSIBLE_TRISTATE_TRUE
        : state.checked == AccessibilityState::Mixed ? GTK_ACCESSIBLE_TRISTATE_MIXED
                                                     : GTK_ACCESSIBLE_TRISTATE_FALSE,
        -1);
  }

  // Value.
  const auto &value = props->accessibilityValue;
  auto number = [&](GtkAccessibleProperty p, const std::optional<int> &v) {
    if (v) {
      gtk_accessible_update_property(acc, p, double(*v), -1);
    } else {
      gtk_accessible_reset_property(acc, p);
    }
  };
  number(GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, value.min);
  number(GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, value.max);
  number(GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, value.now);
  if (value.text) {
    gtk_accessible_update_property(acc, GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT,
                                   value.text->c_str(), -1);
  } else {
    gtk_accessible_reset_property(acc, GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT);
  }

  updateAccessibilityActions(widget, view);

  // A screen reader setting the value: increment or decrement, as on iOS.
  if (RN_IS_RANGE_VIEW(widget) && !g_object_get_qdata(G_OBJECT(widget), range_quark())) {
    g_object_set_qdata(G_OBJECT(widget), range_quark(), GINT_TO_POINTER(1));
    struct Data {
      std::weak_ptr<GtkMountingManager> manager;
      Tag tag;
    };
    g_signal_connect_data(
        widget, "set-accessible-value",
        G_CALLBACK(+[](RNView *, double value, gpointer data) -> gboolean {
          auto *d = static_cast<Data *>(data);
          auto self = d->manager.lock();
          if (!self) return FALSE;
          auto it = self->shadowViews_.find(d->tag);
          if (it == self->shadowViews_.end()) return FALSE;
          auto p = std::dynamic_pointer_cast<const ViewProps>(it->second.props);
          auto emitter = std::dynamic_pointer_cast<const BaseViewEventEmitter>(
              it->second.eventEmitter);
          if (!p || !emitter || !p->accessibilityValue.now) return FALSE;
          double now = *p->accessibilityValue.now;
          if (value == now) return TRUE;
          emitter->onAccessibilityAction(value > now ? "increment" : "decrement");
          return TRUE;
        }),
        new Data{weak_from_this(), view.tag},
        +[](gpointer d, GClosure *) { delete static_cast<Data *>(d); },
        GConnectFlags(0));
  }
}

// accessibilityActions (and "activate" for a pressable view) as actions on
// the widget, which GTK lists in AT-SPI's Action interface.
void GtkMountingManager::updateAccessibilityActions(GtkWidget *widget,
                                                    const ShadowView &view) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  if (!props) return;
  std::vector<std::string> names;
  for (const auto &action : props->accessibilityActions) names.push_back(action.name);
  bool presses = props->events.bits[static_cast<size_t>(ViewEvents::Offset::Click)];
  bool activate_listed =
      std::find(names.begin(), names.end(), "activate") != names.end();
  if (presses && !activate_listed) names.push_back("activate");

  std::string key;
  for (const auto &n : names) key += n + '\n';
  auto *current = static_cast<std::string *>(
      g_object_get_qdata(G_OBJECT(widget), actions_quark()));
  if (current ? *current == key : key.empty()) return;
  g_object_set_qdata_full(G_OBJECT(widget), actions_quark(), new std::string(key),
                          [](gpointer p) { delete static_cast<std::string *>(p); });
  if (names.empty()) {
    gtk_widget_insert_action_group(widget, "a11y", nullptr);
    return;
  }

  struct Data {
    std::weak_ptr<GtkMountingManager> manager;
    Tag tag;
    bool click;  // a press, not onAccessibilityAction
  };
  GSimpleActionGroup *group = g_simple_action_group_new();
  for (const auto &name : names) {
    GSimpleAction *action = g_simple_action_new(name.c_str(), nullptr);
    bool click = name == "activate" && !activate_listed;
    g_signal_connect_data(
        action, "activate",
        G_CALLBACK(+[](GSimpleAction *a, GVariant *, gpointer data) {
          auto *d = static_cast<Data *>(data);
          auto self = d->manager.lock();
          if (!self) return;
          auto it = self->shadowViews_.find(d->tag);
          if (it == self->shadowViews_.end()) return;
          if (d->click) {
            // Like Enter: a click without pointerType, so Pressability
            // calls onPress.
            if (it->second.eventEmitter) {
              it->second.eventEmitter->dispatchEvent(
                  "click", folly::dynamic::object(), RawEvent::Category::Discrete);
            }
          } else if (auto emitter = std::dynamic_pointer_cast<const BaseViewEventEmitter>(
                         it->second.eventEmitter)) {
            emitter->onAccessibilityAction(g_action_get_name(G_ACTION(a)));
          }
        }),
        new Data{weak_from_this(), view.tag, click},
        +[](gpointer d, GClosure *) { delete static_cast<Data *>(d); },
        GConnectFlags(0));
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
    g_object_unref(action);
  }
  gtk_widget_insert_action_group(widget, "a11y", G_ACTION_GROUP(group));
  g_object_unref(group);
}

// A Text: its accessible name is its text (unless labelled), and a change
// inside a live region is announced.
void GtkMountingManager::updateTextAccessibility(GtkWidget *widget,
                                                 const ShadowView &view,
                                                 const std::string &text) {
  auto props = std::dynamic_pointer_cast<const ViewProps>(view.props);
  auto *acc = GTK_ACCESSIBLE(widget);
  auto *previous =
      static_cast<std::string *>(g_object_get_qdata(G_OBJECT(widget), text_quark()));
  bool changed = !previous || *previous != text;
  if (!changed) return;
  bool first = !previous;
  g_object_set_qdata_full(G_OBJECT(widget), text_quark(), new std::string(text),
                          [](gpointer p) { delete static_cast<std::string *>(p); });
  if (!props || props->accessibilityLabel.empty()) set_label(acc, text);
  refreshContentLabels(widget);
  if (first || text.empty()) return;

  // The Text's own live region, or the nearest ancestor's.
  AccessibilityLiveRegion live = AccessibilityLiveRegion::None;
  for (GtkWidget *w = widget; w && live == AccessibilityLiveRegion::None;
       w = gtk_widget_get_parent(w)) {
    auto t = targetForView(w);
    if (t.tag == 0) continue;
    if (auto p = std::dynamic_pointer_cast<const ViewProps>(propsForTag(t.tag))) {
      live = p->accessibilityLiveRegion;
    }
  }
  if (live != AccessibilityLiveRegion::None) {
    announce(widget, text,
             live == AccessibilityLiveRegion::Assertive
                 ? GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_HIGH
                 : GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
  }
}

void GtkMountingManager::announce(GtkWidget *from, const std::string &text,
                                  GtkAccessibleAnnouncementPriority priority) {
  lastAnnouncement_ = text;
  gtk_accessible_announce(GTK_ACCESSIBLE(from), text.c_str(), priority);
}

// `accessible` views named by their content: after text under `from`
// changed (or children came and went), their names follow.
void GtkMountingManager::refreshContentLabels(GtkWidget *from) {
  for (GtkWidget *w = from; w; w = gtk_widget_get_parent(w)) {
    if (!g_object_get_qdata(G_OBJECT(w), content_label_quark())) continue;
    std::string label;
    collect_text(w, label);
    set_label(GTK_ACCESSIBLE(w), label);
  }
}

}  // namespace rngtk

namespace rngtk {

void GtkMountingManager::setScreenReaderActive(bool active) {
  if (active == screenReaderActive_) return;
  screenReaderActive_ = active;
  for (const auto &[tag, widget] : views_) updateScreenReaderFocus(widget);
}

// Whether the widget is an accessible element of its own that only takes
// focus for the screen reader: accessible, not focusable already, not
// hidden, and not inside an element that speaks for it (an `accessible`
// View, a Pressable).
void GtkMountingManager::updateScreenReaderFocus(GtkWidget *widget) {
  static GQuark mine = g_quark_from_static_string("rngtk-a11y-reader-focus");
  if (!RN_IS_VIEW(widget) && !RN_IS_TEXT(widget)) return;
  auto t = targetForView(widget);
  auto props = std::dynamic_pointer_cast<const ViewProps>(propsForTag(t.tag));
  // A Text is an element as on iOS (Text.js only sends `accessible` off
  // iOS and Android when the app sets it, so it can't say "default").
  bool element = props && (props->accessible || RN_IS_TEXT(widget));
  bool eligible = screenReaderActive_ && element &&
                  !(RN_IS_VIEW(widget) && props->focusable) &&
                  gtk_widget_get_parent(widget) != nullptr;
  for (GtkWidget *w = gtk_widget_get_parent(widget); eligible && w;
       w = gtk_widget_get_parent(w)) {
    auto at = targetForView(w);
    if (at.tag == 0) continue;
    auto p = std::dynamic_pointer_cast<const ViewProps>(propsForTag(at.tag));
    if (p && (p->accessible || p->focusable || p->accessibilityElementsHidden ||
              p->importantForAccessibility == ImportantForAccessibility::NoHideDescendants)) {
      eligible = false;
    }
  }
  bool had = g_object_get_qdata(G_OBJECT(widget), mine);
  // (A props update resets focusable to the props' value: set it again.)
  if (eligible || had) gtk_widget_set_focusable(widget, eligible);
  if (eligible == had) return;
  g_object_set_qdata(G_OBJECT(widget), mine, GINT_TO_POINTER(eligible));
  // Descendants may now be (or no longer be) inside an element.
  for (GtkWidget *c = gtk_widget_get_first_child(widget); c;
       c = gtk_widget_get_next_sibling(c)) {
    updateScreenReaderFocus(c);
  }
}

}  // namespace rngtk
