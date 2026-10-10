// react-native-screens' native components on GTK4 and libadwaita, for
// React Navigation's native-stack:
//
// - RNSScreenStack: a GtkOverlay holding layers, each an AdwNavigationView.
//   The stack's screens (its React children, in order) are pages: pushing
//   and popping in React slides them in and out, and the user goes back
//   with the header's back button, a swipe, or Escape, which tells React
//   (onDismissed) so its state follows. A modal screen (presentation
//   'modal', 'formSheet', 'transparentModal'...) starts a layer of its own
//   over the ones below, and screens pushed after it go into that layer.
// - RNSScreen: the host's view for the screen's content, in an
//   AdwToolbarView with an AdwHeaderBar on top (from its header config).
//   The content extends under the header; the screen's shadow node pads it
//   by the header's height (the header's real height comes back through
//   the node's state), unless the header is hidden or translucent.
// - RNSScreenStackHeaderConfig: the header's props (title, colors, back
//   button, hidden, translucent); its RNSScreenStackHeaderSubviews (React
//   views for headerLeft, headerTitle, headerRight) go into the header bar.
// - RNSScreenContainer: screens for tabs and drawers; only the active ones
//   show.
// - RNSScreenContentWrapper and RNSScreenFooter: plain views.
//
// libadwaita is linked here only; the host stays plain GTK, and its theme
// draws the header bars.
#include <adwaita.h>
#include <rngtk/Extensions.h>

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace facebook::react;
namespace yoga = facebook::yoga;

namespace rngtk_screens {

// ---- Shadow nodes -----------------------------------------------------------

// Props as the JS sends them, merged across updates (the library has many,
// read only here).
class RawViewProps final : public ViewProps {
 public:
  RawViewProps() = default;
  RawViewProps(const PropsParserContext &context, const RawViewProps &sourceProps,
               const RawProps &rawProps)
      : ViewProps(context, sourceProps, rawProps), raw(sourceProps.raw) {
    folly::dynamic changed = rawProps.toDynamic();
    if (changed.isObject()) {
      for (auto &[key, value] : changed.items()) raw[key] = value;
    }
  }
  folly::dynamic raw = folly::dynamic::object;

  std::string string(const char *key, const std::string &fallback = "") const {
    auto it = raw.find(key);
    return it != raw.items().end() && it->second.isString() ? it->second.getString() : fallback;
  }
  bool boolean(const char *key, bool fallback = false) const {
    auto it = raw.find(key);
    return it != raw.items().end() && it->second.isBool() ? it->second.getBool() : fallback;
  }
  double number(const char *key, double fallback = 0) const {
    auto it = raw.find(key);
    return it != raw.items().end() && it->second.isNumber() ? it->second.asDouble() : fallback;
  }
  // A processed color (ARGB as a number), if set.
  bool color(const char *key, GdkRGBA *out) const {
    auto it = raw.find(key);
    if (it == raw.items().end() || !it->second.isNumber()) return false;
    auto argb = uint32_t(int64_t(it->second.asDouble()));
    *out = GdkRGBA{((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                   (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f};
    return true;
  }
};

class ScreensEventEmitter : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;
  // "appear" reaches JS as onAppear.
  void emit(const std::string &type, folly::dynamic payload = folly::dynamic::object()) const {
    dispatchEvent(type, std::move(payload));
  }
};

// The header height GTK measured for a screen (-1: not yet), and for a
// form sheet the size of its card.
struct ScreenState {
  float headerHeight = -1;
  Size frameSize{};
};

// Before a screen's header has been measured: a header bar's height in
// GTK's default themes.
std::atomic<float> defaultHeaderHeight{47};

extern const char ScreenComponentName[] = "RNSScreen";
extern const char HeaderConfigComponentName[] = "RNSScreenStackHeaderConfig";

class ScreenShadowNode final
    : public ConcreteViewShadowNode<ScreenComponentName, RawViewProps, ScreensEventEmitter,
                                    ScreenState> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  // The content starts under the header, unless it's hidden or
  // translucent (then the content runs under it).
  void updateHeaderPadding() {
    float top = 0;
    for (const auto &child : getChildren()) {
      if (std::strcmp(child->getComponentName(), HeaderConfigComponentName) != 0) continue;
      auto props = std::static_pointer_cast<const RawViewProps>(child->getProps());
      if (!props->boolean("hidden") && !props->boolean("translucent")) {
        float measured = getStateData().headerHeight;
        top = measured >= 0 ? measured : defaultHeaderHeight.load();
      }
    }
    auto style = yogaNode_.style();
    if (style.padding(yoga::Edge::Top) != yoga::StyleLength::points(top)) {
      style.setPadding(yoga::Edge::Top, yoga::StyleLength::points(top));
      yogaNode_.setStyle(style);
      yogaNode_.setDirty(true);
    }
  }

  void appendChild(const std::shared_ptr<const ShadowNode> &child) override {
    ConcreteViewShadowNode::appendChild(child);
    updateHeaderPadding();
  }
};

class ScreenComponentDescriptor final : public ConcreteComponentDescriptor<ScreenShadowNode> {
 public:
  using ConcreteComponentDescriptor::ConcreteComponentDescriptor;
  void adopt(ShadowNode &shadowNode) const override {
    auto &screen = static_cast<ScreenShadowNode &>(shadowNode);
    auto size = screen.getStateData().frameSize;
    if (size.width > 0 && size.height > 0) screen.setSize(size);
    screen.updateHeaderPadding();
    ConcreteComponentDescriptor::adopt(shadowNode);
  }
};

#define RNGTK_SCREENS_PLAIN(X) \
  X(ScreenStack, "RNSScreenStack") \
  X(HeaderConfig2, "RNSScreenStackHeaderConfig") \
  X(HeaderSubview, "RNSScreenStackHeaderSubview") \
  X(ScreenContainer, "RNSScreenContainer") \
  X(ContentWrapper, "RNSScreenContentWrapper") \
  X(ScreenFooter, "RNSScreenFooter") \
  X(FullWindowOverlay, "RNSFullWindowOverlay") \
  X(SearchBar, "RNSSearchBar")

#define RNGTK_SCREENS_DECLARE(Name, js)                                                       \
  extern const char Name##ComponentName[] = js;                                              \
  using Name##ShadowNode = ConcreteViewShadowNode<Name##ComponentName, RawViewProps, ScreensEventEmitter>; \
  using Name##ComponentDescriptor = ConcreteComponentDescriptor<Name##ShadowNode>;
RNGTK_SCREENS_PLAIN(RNGTK_SCREENS_DECLARE)
#undef RNGTK_SCREENS_DECLARE

// ---- GTK side ---------------------------------------------------------------

namespace {

struct Stack;

// A screen: the host's view (an RNView) in a toolbar view, in a page.
struct Screen {
  GtkWidget *view = nullptr;  // the host's; not ours
  AdwNavigationPage *page = nullptr;
  AdwToolbarView *toolbar = nullptr;
  AdwHeaderBar *header = nullptr;
  std::string cssClass;
  ShadowView shadow;
  Stack *stack = nullptr;  // the stack it's in, if any
  bool dismissed = false;  // the user went back from it; React hasn't yet
  bool headerDirty = true;
  float reportedHeaderHeight = -1;
  Size sheetSize{};  // a form sheet's card (else 0x0: the stack's)
  // Header subviews in the header bar now.
  std::vector<GtkWidget *> packed;
  // Our back button, for screens the user can't pop natively.
  GtkWidget *backButton = nullptr;
  // The search bar's entry, in a top bar of its own under the header.
  GtkWidget *searchEntry = nullptr;
  GtkWidget *searchRow = nullptr;

  std::shared_ptr<const ScreensEventEmitter> emitter() const {
    return std::static_pointer_cast<const ScreensEventEmitter>(shadow.eventEmitter);
  }
  const RawViewProps *props() const {
    return static_cast<const RawViewProps *>(shadow.props.get());
  }
  void emit(const char *type, folly::dynamic payload = folly::dynamic::object()) const {
    if (auto e = emitter()) e->emit(type, std::move(payload));
  }
};

// A header config: its props and subviews.
struct HeaderConfig {
  ShadowView shadow;
  std::vector<GtkWidget *> subviews;  // the host's views, in order
  const RawViewProps *props() const {
    return static_cast<const RawViewProps *>(shadow.props.get());
  }
};

// A header subview: which slot it goes in.
struct HeaderSubview {
  std::string type = "left";
};

struct Layer {
  GtkWidget *container = nullptr;  // in the stack's overlay (layer 0: the nav view)
  AdwNavigationView *nav = nullptr;
  std::string presentation = "push";
  std::vector<Screen *> screens;  // what the nav view shows, bottom first
};

struct Stack {
  GtkWidget *overlay = nullptr;
  ShadowView shadow;
  std::vector<GtkWidget *> children;  // the screens' views, in React's order
  std::vector<Layer> layers;
  guint idle = 0;
  bool syncing = false;  // our own push/pop: don't tell React
};

constexpr const char *kScreen = "rngtk-screens-screen";
constexpr const char *kHeaderConfig = "rngtk-screens-header-config";
constexpr const char *kHeaderSubview = "rngtk-screens-header-subview";
constexpr const char *kStack = "rngtk-screens-stack";

Screen *screen_of(GtkWidget *view) {
  return view ? static_cast<Screen *>(g_object_get_data(G_OBJECT(view), kScreen)) : nullptr;
}
HeaderConfig *config_of(GtkWidget *w) {
  return w ? static_cast<HeaderConfig *>(g_object_get_data(G_OBJECT(w), kHeaderConfig)) : nullptr;
}
HeaderSubview *subview_of(GtkWidget *w) {
  return w ? static_cast<HeaderSubview *>(g_object_get_data(G_OBJECT(w), kHeaderSubview)) : nullptr;
}
Stack *stack_of(GtkWidget *w) {
  return w ? static_cast<Stack *>(g_object_get_data(G_OBJECT(w), kStack)) : nullptr;
}

std::string presentation_of(const Screen *s) {
  return s->props() ? s->props()->string("stackPresentation", "push") : "push";
}
bool is_modal(const std::string &p) { return p != "push"; }
bool active(const Screen *s) {
  // activityState 0: a preloaded screen, not on the stack yet.
  return !s->props() || s->props()->number("activityState", -1) != 0;
}

// ---- Header colors (one provider for every header bar) ---------------------

GtkCssProvider *css_provider() {
  static GtkCssProvider *provider = [] {
    auto *p = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return p;
  }();
  return provider;
}
std::map<std::string, std::string> &css_rules() {
  static std::map<std::string, std::string> rules;
  return rules;
}
void set_css(const std::string &cls, const std::string &rule) {
  auto &rules = css_rules();
  if (rule.empty() ? !rules.erase(cls) : rules[cls] == rule) return;
  if (!rule.empty()) rules[cls] = rule;
  std::string css;
  for (auto &[c, r] : rules) css += r;
  gtk_css_provider_load_from_string(css_provider(), css.c_str());
}
std::string css_color(const GdkRGBA &c) {
  char *s = gdk_rgba_to_string(&c);
  std::string out = s;
  g_free(s);
  return out;
}

// ---- Screens ------------------------------------------------------------------

void schedule_sync(Stack *stack);
void refresh_headers();

HeaderConfig *find_config(Screen *s) {
  for (GtkWidget *c = gtk_widget_get_first_child(s->view); c; c = gtk_widget_get_next_sibling(c)) {
    if (HeaderConfig *config = config_of(c)) return config;
  }
  return nullptr;
}

std::set<Screen *> &live_screens() {
  static std::set<Screen *> screens;
  return screens;
}

void screen_free(gpointer data) {
  auto *s = static_cast<Screen *>(data);
  live_screens().erase(s);
  set_css(s->cssClass, "");
  for (GtkWidget *w : s->packed) {
    if (adw_header_bar_get_title_widget(s->header) == w) {
      adw_header_bar_set_title_widget(s->header, nullptr);
    } else if (gtk_widget_get_parent(w)) {
      adw_header_bar_remove(s->header, w);
    }
    g_object_unref(w);
  }
  if (s->page) {
    // The view stays the host's: take it out of the toolbar view.
    if (gtk_widget_get_parent(s->view)) adw_toolbar_view_set_content(s->toolbar, nullptr);
    g_object_unref(s->page);
  }
  delete s;
}

// libadwaita's transition shadow makes its gizmos visible only after
// allocating them, so GTK warns about snapshotting them unallocated on a
// transition's first frame: keep them visible between transitions (the
// view hides them then anyway, with child-visible).
void prime_shadow(GtkWidget *nav) {
  for (GtkWidget *c = gtk_widget_get_first_child(nav); c; c = gtk_widget_get_next_sibling(c)) {
    const char *name = gtk_widget_get_css_name(c);
    for (const char *gizmo : {"dimming", "shadow", "border", "outline"}) {
      if (!strcmp(name, gizmo)) gtk_widget_set_visible(c, TRUE);
    }
  }
}

// After a transition (the user's too: the back button, a swipe).
void prime_shadow_later(AdwNavigationPage *page) {
  GtkWidget *nav = gtk_widget_get_ancestor(GTK_WIDGET(page), ADW_TYPE_NAVIGATION_VIEW);
  if (!nav) return;
  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer nav) -> gboolean {
    prime_shadow(GTK_WIDGET(nav));
    return G_SOURCE_REMOVE;
  }, g_object_ref(nav), g_object_unref);
}

// The page's transitions are the screen's appear and disappear events.
void connect_page(Screen *s) {
  g_signal_connect(s->page, "showing", G_CALLBACK(+[](AdwNavigationPage *, Screen *s) {
                     s->emit("willAppear");
                   }), s);
  g_signal_connect(s->page, "shown", G_CALLBACK(+[](AdwNavigationPage *page, Screen *s) {
                     prime_shadow_later(page);
                     s->emit("appear");
                     s->emit("transitionProgress",
                             folly::dynamic::object("progress", 1)("closing", 0)("goingForward", 1));
                     if (s->stack) {
                       if (auto e = std::static_pointer_cast<const ScreensEventEmitter>(
                               s->stack->shadow.eventEmitter)) {
                         e->emit("finishTransitioning");
                       }
                     }
                   }), s);
  g_signal_connect(s->page, "hiding", G_CALLBACK(+[](AdwNavigationPage *, Screen *s) {
                     s->emit("willDisappear");
                   }), s);
  g_signal_connect(s->page, "hidden", G_CALLBACK(+[](AdwNavigationPage *, Screen *s) {
                     s->emit("disappear");
                   }), s);
}

Screen *ensure_screen(GtkWidget *view) {
  if (Screen *s = screen_of(view)) return s;
  auto *s = new Screen();
  s->view = view;
  static int next = 0;
  s->cssClass = "rngtk-screen-" + std::to_string(++next);
  s->header = ADW_HEADER_BAR(adw_header_bar_new());
  // The window has its own title bar.
  adw_header_bar_set_show_start_title_buttons(s->header, FALSE);
  adw_header_bar_set_show_end_title_buttons(s->header, FALSE);
  gtk_widget_add_css_class(GTK_WIDGET(s->header), s->cssClass.c_str());
  s->toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
  adw_toolbar_view_add_top_bar(s->toolbar, GTK_WIDGET(s->header));
  adw_toolbar_view_set_extend_content_to_top_edge(s->toolbar, TRUE);
  s->page = ADW_NAVIGATION_PAGE(g_object_ref_sink(adw_navigation_page_new(GTK_WIDGET(s->toolbar), " ")));
  connect_page(s);
  g_object_set_data_full(G_OBJECT(view), kScreen, s, screen_free);
  live_screens().insert(s);
  return s;
}

// The screen's view goes in its page (for a stack) or straight in the
// parent (a ScreenContainer, which the host lays out).
void put_in_page(Screen *s) {
  if (gtk_widget_get_parent(s->view) == nullptr) adw_toolbar_view_set_content(s->toolbar, s->view);
}

// The header bar from the screen's header config.
void refresh_header(Screen *s) {
  HeaderConfig *config = find_config(s);
  const RawViewProps *p = config ? config->props() : nullptr;
  bool hidden = !config || !p || p->boolean("hidden");
  adw_toolbar_view_set_reveal_top_bars(s->toolbar, !hidden);
  if (!config || !p) return;

  std::string title = p->string("title");
  adw_navigation_page_set_title(s->page, title.empty() ? " " : title.c_str());

  // usePreventRemove (preventNativeDismiss) and gestureEnabled: false: no
  // swipe, Escape or libadwaita back button; our own back button asks
  // React (onHeaderBackButtonClicked), which may refuse.
  const RawViewProps *sp = s->props();
  bool canPop = !(sp && (sp->boolean("preventNativeDismiss") || !sp->boolean("gestureEnabled", true)));
  adw_navigation_page_set_can_pop(s->page, canPop);
  bool showBack = !p->boolean("hideBackButton");
  adw_header_bar_set_show_back_button(s->header, showBack && canPop);
  GtkWidget *nav = gtk_widget_get_ancestor(GTK_WIDGET(s->page), ADW_TYPE_NAVIGATION_VIEW);
  bool hasPrevious = nav && adw_navigation_view_get_previous_page(ADW_NAVIGATION_VIEW(nav), s->page);
  if (!canPop && showBack && hasPrevious && !s->backButton) {
    s->backButton = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_set_tooltip_text(s->backButton, "Back");
    gtk_widget_add_css_class(s->backButton, "back");
    g_signal_connect(s->backButton, "clicked", G_CALLBACK(+[](GtkButton *, Screen *s) {
                       s->emit("headerBackButtonClicked");
                     }), s);
    adw_header_bar_pack_start(s->header, s->backButton);
  }
  if (s->backButton) gtk_widget_set_visible(s->backButton, !canPop && showBack && hasPrevious);

  // Subviews: left after the back button, center as the title, right at
  // the end. A left view carries the title on Android-style headers.
  GtkWidget *left = nullptr, *center = nullptr;
  std::vector<GtkWidget *> rights;
  for (GtkWidget *w : config->subviews) {
    HeaderSubview *sv = subview_of(w);
    std::string type = sv ? sv->type : "left";
    if (type == "left" && !left) left = w;
    else if (type == "center" && !center) center = w;
    else if (type == "right") rights.push_back(w);
  }
  // A search bar subview: its RNSSearchBar (a GtkSearchEntry).
  GtkWidget *search = nullptr;
  for (GtkWidget *w : config->subviews) {
    HeaderSubview *sv = subview_of(w);
    if (!sv || sv->type != "searchBar") continue;
    for (GtkWidget *c = gtk_widget_get_first_child(w); c && !search; c = gtk_widget_get_next_sibling(c)) {
      if (GTK_IS_SEARCH_ENTRY(c)) search = c;
    }
    if (!search && s->searchEntry && GTK_IS_SEARCH_ENTRY(s->searchEntry)) search = s->searchEntry;
  }
  // The search entry: in a bar of its own under the header.
  if (search && gtk_widget_get_parent(search) != s->searchRow) {
    if (!s->searchRow) {
      s->searchRow = gtk_center_box_new();
      gtk_widget_add_css_class(s->searchRow, "toolbar");
      adw_toolbar_view_add_top_bar(s->toolbar, s->searchRow);
    }
    g_object_ref(search);
    if (gtk_widget_get_parent(search)) gtk_widget_unparent(search);
    gtk_widget_set_size_request(search, 360, -1);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(s->searchRow), search);
    g_object_unref(search);
    s->searchEntry = search;
  }
  if (s->searchRow) gtk_widget_set_visible(s->searchRow, search != nullptr);
  std::vector<GtkWidget *> want;
  if (left) want.push_back(left);
  for (GtkWidget *r : rights) want.push_back(r);
  if (center) want.push_back(center);
  if (want != s->packed) {
    for (GtkWidget *w : s->packed) {
      if (adw_header_bar_get_title_widget(s->header) == w) {
        adw_header_bar_set_title_widget(s->header, nullptr);
      } else if (gtk_widget_get_parent(w)) {
        adw_header_bar_remove(s->header, w);
      }
      g_object_unref(w);
    }
    s->packed.clear();
    for (GtkWidget *w : want) {
      g_object_ref(w);
      if (gtk_widget_get_parent(w)) gtk_widget_unparent(w);
      gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
      if (w == left) adw_header_bar_pack_start(s->header, w);
      else if (w == center) adw_header_bar_set_title_widget(s->header, w);
      else adw_header_bar_pack_end(s->header, w);
      s->packed.push_back(w);
    }
  }
  adw_header_bar_set_show_title(s->header, left == nullptr || center != nullptr);

  // Colors and the title's font.
  std::string rule;
  GdkRGBA c;
  std::string sel = "headerbar." + s->cssClass;
  bool translucent = p->boolean("translucent");
  if (p->color("backgroundColor", &c)) {
    rule += sel + " { background: " + css_color(c) + "; box-shadow: none; }";
  } else if (translucent) {
    rule += sel + " { background: transparent; box-shadow: none; }";
  }
  if (p->boolean("hideShadow")) rule += sel + " { box-shadow: none; }";
  if (p->color("color", &c)) {
    // The tint: the back button's icon, flat on the header's color.
    GdkRGBA hover = c;
    hover.alpha = 0.15f;
    rule += sel + " button { color: " + css_color(c) +
            "; background: none; box-shadow: none; border-color: transparent; }" + sel +
            " button:hover { background: " + css_color(hover) + "; }";
  }
  std::string font;
  if (p->color("titleColor", &c)) font += "color: " + css_color(c) + ";";
  if (double size = p->number("titleFontSize"); size > 0) {
    font += "font-size: " + std::to_string(int(size)) + "px;";
  }
  if (std::string family = p->string("titleFontFamily"); !family.empty()) {
    font += "font-family: \"" + family + "\";";
  }
  if (std::string weight = p->string("titleFontWeight"); !weight.empty()) {
    font += "font-weight: " + (weight == "normal" || weight == "bold" ? weight : weight) + ";";
  }
  if (!font.empty()) rule += sel + " .title { " + font + " }";
  set_css(s->cssClass, rule);
}

// What GTK decided (the header's height, a sheet's size) to the screen's
// shadow node. One update with both: Fabric keeps only a node's last
// pending state update.
void push_state(Screen *s) {
  auto state = std::static_pointer_cast<const ScreenShadowNode::ConcreteState>(s->shadow.state);
  if (!state) return;
  float height = s->reportedHeaderHeight;
  Size size = s->sheetSize;
  state->updateState([height, size](const ScreenState &old) -> StateData::Shared {
    if (old.headerHeight == height && old.frameSize == size) return nullptr;
    auto data = std::make_shared<ScreenState>(old);
    data->headerHeight = height;
    data->frameSize = size;
    return data;
  });
}

// The header's real height back to the shadow node (it pads the content
// by it) and to JS (useHeaderHeight).
void report_header_height(Screen *s) {
  HeaderConfig *config = find_config(s);
  bool shown = config && config->props() && !config->props()->boolean("hidden");
  float height = 0;
  if (shown) {
    for (GtkWidget *bar : {GTK_WIDGET(s->header), s->searchRow}) {
      if (!bar || !gtk_widget_get_visible(bar)) continue;
      int min = 0, nat = 0;
      gtk_widget_measure(bar, GTK_ORIENTATION_VERTICAL, -1, &min, &nat, nullptr, nullptr);
      height += float(nat);
    }
  }
  if (height == s->reportedHeaderHeight) return;
  s->reportedHeaderHeight = height;
  if (height > 0) defaultHeaderHeight = height;
  push_state(s);
  s->emit("headerHeightChange", folly::dynamic::object("headerHeight", height));
}

guint headers_idle = 0;
void refresh_headers() {
  if (headers_idle) return;
  headers_idle = g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer) -> gboolean {
    headers_idle = 0;
    for (Screen *s : live_screens()) {
      if (!s->headerDirty) continue;
      s->headerDirty = false;
      refresh_header(s);
      report_header_height(s);
    }
    return G_SOURCE_REMOVE;
  }, nullptr, nullptr);
}
void mark_all_headers() {
  for (Screen *s : live_screens()) s->headerDirty = true;
  refresh_headers();
}

// ---- Stacks -------------------------------------------------------------------

// The pages a layer's nav view has now.
std::vector<AdwNavigationPage *> nav_pages(AdwNavigationView *nav) {
  std::vector<AdwNavigationPage *> pages;
  GListModel *model = adw_navigation_view_get_navigation_stack(nav);
  for (guint i = 0, n = g_list_model_get_n_items(model); i < n; i++) {
    auto *page = ADW_NAVIGATION_PAGE(g_list_model_get_item(model, i));
    pages.push_back(page);
    g_object_unref(page);
  }
  g_object_unref(model);
  return pages;
}

Screen *screen_for_page(Stack *stack, AdwNavigationPage *page) {
  for (GtkWidget *v : stack->children) {
    if (Screen *s = screen_of(v); s && s->page == page) return s;
  }
  return nullptr;
}

// The user went back (the header's back button, a swipe, Escape): React
// pops the screen too.
void on_popped(AdwNavigationView *nav, AdwNavigationPage *page, Stack *stack) {
  prime_shadow(GTK_WIDGET(nav));
  if (stack->syncing) return;
  Screen *s = screen_for_page(stack, page);
  if (!s || s->dismissed) return;
  s->dismissed = true;
  s->emit("dismissed", folly::dynamic::object("dismissCount", 1));
}

AdwNavigationView *new_nav(Stack *stack) {
  auto *nav = ADW_NAVIGATION_VIEW(adw_navigation_view_new());
  g_signal_connect(nav, "popped", G_CALLBACK(on_popped), stack);
  return nav;
}

// A modal layer: Escape dismisses it (all its screens).
gboolean on_layer_escape(GtkWidget *widget, GVariant *, gpointer data) {
  auto *stack = static_cast<Stack *>(data);
  for (auto &layer : stack->layers) {
    if (layer.container != widget || layer.screens.empty()) continue;
    // Escape inside a deeper page pops it (the nav view's own); here only
    // the layer's first screen is left.
    if (layer.screens.size() > 1) return FALSE;
    Screen *first = layer.screens.front();
    if (first->props() && first->props()->boolean("preventNativeDismiss")) {
      first->emit("nativeDismissCancelled", folly::dynamic::object("dismissCount", 1));
      return TRUE;
    }
    for (Screen *s : layer.screens) s->dismissed = true;
    first->emit("dismissed", folly::dynamic::object("dismissCount", int(layer.screens.size())));
    schedule_sync(stack);
    return TRUE;
  }
  return FALSE;
}

Layer make_layer(Stack *stack, const std::string &presentation) {
  Layer layer;
  layer.presentation = presentation;
  layer.nav = new_nav(stack);
  // A modal slides up from the bottom; a form sheet is a card over a dimmed
  // backdrop; a transparent modal shows what's below.
  auto *revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
  gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 250);
  if (presentation == "formSheet" || presentation == "pageSheet") {
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    auto *backdrop = gtk_overlay_new();
    gtk_widget_add_css_class(backdrop, "rngtk-sheet-backdrop");
    auto *card = GTK_WIDGET(layer.nav);
    gtk_widget_add_css_class(card, "rngtk-sheet");
    gtk_widget_set_overflow(card, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_overlay_set_child(GTK_OVERLAY(backdrop), gtk_drawing_area_new());
    gtk_overlay_add_overlay(GTK_OVERLAY(backdrop), card);
    gtk_revealer_set_child(GTK_REVEALER(revealer), backdrop);
    set_css("rngtk-sheet",
            ".rngtk-sheet-backdrop { background: rgba(0,0,0,0.32); }"
            ".rngtk-sheet { border-radius: 12px; box-shadow: 0 4px 24px rgba(0,0,0,0.3); }");
  } else {
    gtk_revealer_set_child(GTK_REVEALER(revealer), GTK_WIDGET(layer.nav));
  }
  if (presentation != "transparentModal" && presentation != "containedTransparentModal") {
    gtk_widget_add_css_class(GTK_WIDGET(layer.nav), "background");
  }
  auto *shortcuts = gtk_shortcut_controller_new();
  gtk_shortcut_controller_add_shortcut(
      GTK_SHORTCUT_CONTROLLER(shortcuts),
      gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, GdkModifierType(0)),
                       gtk_callback_action_new(on_layer_escape, stack, nullptr)));
  gtk_widget_add_controller(revealer, shortcuts);
  layer.container = revealer;
  gtk_overlay_add_overlay(GTK_OVERLAY(stack->overlay), revealer);
  gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
  return layer;
}

void drop_layer(Layer &layer) {
  // Slide out, then go.
  GtkWidget *revealer = layer.container;
  g_signal_connect(revealer, "notify::child-revealed",
                   G_CALLBACK(+[](GtkRevealer *r, GParamSpec *, gpointer) {
                     if (gtk_revealer_get_child_revealed(r)) return;
                     if (GtkWidget *overlay = gtk_widget_get_parent(GTK_WIDGET(r))) {
                       gtk_overlay_remove_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(r));
                     }
                   }),
                   nullptr);
  for (Screen *s : layer.screens) {
    s->emit("willDisappear");
    s->emit("disappear");
  }
  // What's below takes the pointer at once.
  gtk_widget_set_can_target(revealer, FALSE);
  gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
}

// A form sheet's card size: the sheet's detent of the stack's height, at
// most 640 wide. The screen lays out at that size (its state).
void size_sheet(Stack *stack, Layer &layer) {
  if (layer.presentation != "formSheet" && layer.presentation != "pageSheet") return;
  float w = stack->shadow.layoutMetrics.frame.size.width;
  float h = stack->shadow.layoutMetrics.frame.size.height;
  Screen *first = layer.screens.empty() ? nullptr : layer.screens.front();
  double detent = 1.0;
  if (first && first->props()) {
    auto it = first->props()->raw.find("sheetAllowedDetents");
    if (it != first->props()->raw.items().end() && it->second.isArray() && !it->second.empty() &&
        it->second[0].isNumber()) {
      detent = std::clamp(it->second[it->second.size() - 1].asDouble(), 0.1, 1.0);
    }
  }
  Size size{std::min(640.0f, std::round(w * 0.9f)), std::round(std::min(h * 0.9f, float(h * detent)))};
  gtk_widget_set_size_request(GTK_WIDGET(layer.nav), int(size.width), int(size.height));
  for (Screen *s : layer.screens) {
    s->sheetSize = size;
    push_state(s);
  }
}

void sync_layer(Stack *stack, Layer &layer, const std::vector<Screen *> &want) {
  auto *nav = layer.nav;
  std::vector<AdwNavigationPage *> have = nav_pages(nav);
  std::vector<AdwNavigationPage *> pages;
  for (Screen *s : want) {
    put_in_page(s);
    pages.push_back(s->page);
  }
  layer.screens = want;
  size_sheet(stack, layer);
  if (have == pages) return;
  // The top screen's animation: none, or the stack's slide.
  Screen *top = want.empty() ? nullptr : want.back();
  std::string animation = top && top->props() ? top->props()->string("stackAnimation", "default") : "default";
  adw_navigation_view_set_animate_transitions(nav, animation != "none" && !have.empty());
  size_t common = 0;
  while (common < have.size() && common < pages.size() && have[common] == pages[common]) common++;
  prime_shadow(GTK_WIDGET(nav));
  stack->syncing = true;
  if (common == have.size()) {
    for (size_t i = common; i < pages.size(); i++) adw_navigation_view_push(nav, pages[i]);
  } else if (common == pages.size() && common > 0) {
    adw_navigation_view_pop_to_page(nav, pages.back());
  } else {
    adw_navigation_view_replace(nav, pages.data(), int(pages.size()));
  }
  stack->syncing = false;
}

// React's screens to layers and pages.
void sync(Stack *stack) {
  std::vector<std::vector<Screen *>> groups;
  std::vector<std::string> presentations;
  for (GtkWidget *v : stack->children) {
    Screen *s = screen_of(v);
    if (!s || s->dismissed || !active(s)) continue;
    std::string p = presentation_of(s);
    if (groups.empty() || (is_modal(p) && !groups.back().empty())) {
      groups.emplace_back();
      presentations.push_back(groups.size() == 1 ? "push" : p);
    }
    groups.back().push_back(s);
  }
  // Layer 0 always exists: it's the overlay's child.
  if (stack->layers.empty()) {
    Layer base;
    base.nav = new_nav(stack);
    base.container = GTK_WIDGET(base.nav);
    gtk_overlay_set_child(GTK_OVERLAY(stack->overlay), base.container);
    stack->layers.push_back(base);
  }
  if (groups.empty()) groups.emplace_back();
  // Drop the layers above what's left, and those whose presentation changed.
  size_t keep = 1;
  while (keep < stack->layers.size() && keep < groups.size() &&
         stack->layers[keep].presentation == presentations[keep]) {
    keep++;
  }
  while (stack->layers.size() > keep) {
    drop_layer(stack->layers.back());
    stack->layers.pop_back();
  }
  for (size_t i = 0; i < groups.size(); i++) {
    if (i >= stack->layers.size()) {
      stack->layers.push_back(make_layer(stack, presentations[i]));
      for (Screen *s : groups[i]) s->emit("willAppear");
    }
    sync_layer(stack, stack->layers[i], groups[i]);
  }
  for (Screen *s : live_screens()) s->headerDirty = true;
  refresh_headers();
}

void schedule_sync(Stack *stack) {
  if (stack->idle) return;
  stack->idle = g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer data) -> gboolean {
    auto *stack = static_cast<Stack *>(data);
    stack->idle = 0;
    sync(stack);
    return G_SOURCE_REMOVE;
  }, stack, nullptr);
}

void stack_free(gpointer data) {
  auto *stack = static_cast<Stack *>(data);
  if (stack->idle) g_source_remove(stack->idle);
  for (GtkWidget *v : stack->children) {
    if (Screen *s = screen_of(v); s && s->stack == stack) s->stack = nullptr;
  }
  delete stack;
}

}  // namespace

// ---- The components' functions -----------------------------------------------

GtkWidget *create_stack(const ShadowView &) {
  auto *stack = new Stack();
  stack->overlay = gtk_overlay_new();
  gtk_widget_set_overflow(stack->overlay, GTK_OVERFLOW_HIDDEN);
  g_object_set_data_full(G_OBJECT(stack->overlay), kStack, stack, stack_free);
  sync(stack);
  return stack->overlay;
}

void update_stack(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  Stack *stack = stack_of(widget);
  if (!stack) return;
  bool resized = stack->shadow.layoutMetrics.frame.size != view.layoutMetrics.frame.size;
  stack->shadow = view;
  if (resized) schedule_sync(stack);
}

void stack_insert(GtkWidget *parent, GtkWidget *child, int index) {
  Stack *stack = stack_of(parent);
  if (!stack) return;
  Screen *s = ensure_screen(child);
  s->stack = stack;
  s->dismissed = false;
  auto &c = stack->children;
  c.insert(c.begin() + std::clamp(index, 0, int(c.size())), child);
  schedule_sync(stack);
}

void stack_remove(GtkWidget *parent, GtkWidget *child) {
  Stack *stack = stack_of(parent);
  if (!stack) return;
  auto &c = stack->children;
  c.erase(std::remove(c.begin(), c.end(), child), c.end());
  if (Screen *s = screen_of(child)) s->stack = nullptr;
  schedule_sync(stack);
}

void update_screen(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  Screen *s = ensure_screen(widget);
  bool changed = !s->shadow.props || s->shadow.props != view.props;
  s->shadow = view;
  if (changed && s->stack) schedule_sync(s->stack);
  // In a ScreenContainer (tabs, drawers): only active screens show.
  if (!s->stack && !active(s)) gtk_widget_set_visible(widget, FALSE);
  s->headerDirty = true;
  refresh_headers();
}

GtkWidget *create_header_config(const ShadowView &) {
  // Not drawn: its props and subviews go to the screen's header bar.
  GtkWidget *w = gtk_drawing_area_new();
  gtk_widget_set_can_target(w, FALSE);
  g_object_set_data_full(G_OBJECT(w), kHeaderConfig, new HeaderConfig(),
                         [](gpointer p) { delete static_cast<HeaderConfig *>(p); });
  return w;
}

void update_header_config(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  if (HeaderConfig *config = config_of(widget)) config->shadow = view;
  mark_all_headers();
}

void header_config_insert(GtkWidget *parent, GtkWidget *child, int index) {
  HeaderConfig *config = config_of(parent);
  if (!config) return;
  auto &v = config->subviews;
  v.insert(v.begin() + std::clamp(index, 0, int(v.size())), child);
  mark_all_headers();
}

void header_config_remove(GtkWidget *parent, GtkWidget *child) {
  HeaderConfig *config = config_of(parent);
  if (!config) return;
  auto &v = config->subviews;
  v.erase(std::remove(v.begin(), v.end(), child), v.end());
  for (Screen *s : live_screens()) {
    auto it = std::find(s->packed.begin(), s->packed.end(), child);
    if (it == s->packed.end()) continue;
    if (gtk_widget_get_parent(child) == GTK_WIDGET(s->header) ||
        adw_header_bar_get_title_widget(s->header) == child) {
      if (adw_header_bar_get_title_widget(s->header) == child) {
        adw_header_bar_set_title_widget(s->header, nullptr);
      } else {
        adw_header_bar_remove(s->header, child);
      }
    }
    g_object_unref(child);
    s->packed.erase(it);
  }
  mark_all_headers();
}

void update_header_subview(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  auto *sv = subview_of(widget);
  if (!sv) {
    sv = new HeaderSubview();
    g_object_set_data_full(G_OBJECT(widget), kHeaderSubview, sv,
                           [](gpointer p) { delete static_cast<HeaderSubview *>(p); });
  }
  auto props = std::static_pointer_cast<const RawViewProps>(view.props);
  std::string type = props ? props->string("type", "left") : "left";
  // Its size is its Yoga frame (an RNView's natural size).
  gtk_widget_queue_resize(widget);
  if (type != sv->type) {
    sv->type = type;
    mark_all_headers();
  }
}

// RNSSearchBar: a GtkSearchEntry (the header puts it in its title).
namespace {
struct SearchBarData {
  std::shared_ptr<const ScreensEventEmitter> emitter;
  bool settingText = false;
};
SearchBarData *search_of(GtkWidget *w) {
  return static_cast<SearchBarData *>(g_object_get_data(G_OBJECT(w), "rngtk-screens-search"));
}
void search_emit(GtkWidget *w, const char *type, folly::dynamic payload = folly::dynamic::object()) {
  if (SearchBarData *d = search_of(w); d && d->emitter) d->emitter->emit(type, std::move(payload));
}
std::string entry_text(GtkWidget *w) { return gtk_editable_get_text(GTK_EDITABLE(w)); }
}  // namespace

GtkWidget *create_search_bar(const ShadowView &) {
  GtkWidget *entry = gtk_search_entry_new();
  g_object_set_data_full(G_OBJECT(entry), "rngtk-screens-search", new SearchBarData(),
                         [](gpointer p) { delete static_cast<SearchBarData *>(p); });
  g_signal_connect(entry, "search-changed", G_CALLBACK(+[](GtkSearchEntry *e, gpointer) {
                     SearchBarData *d = search_of(GTK_WIDGET(e));
                     if (d && !d->settingText) {
                       search_emit(GTK_WIDGET(e), "changeText",
                                   folly::dynamic::object("text", entry_text(GTK_WIDGET(e))));
                     }
                   }), nullptr);
  g_signal_connect(entry, "activate", G_CALLBACK(+[](GtkSearchEntry *e, gpointer) {
                     search_emit(GTK_WIDGET(e), "searchButtonPress",
                                 folly::dynamic::object("text", entry_text(GTK_WIDGET(e))));
                   }), nullptr);
  g_signal_connect(entry, "stop-search", G_CALLBACK(+[](GtkSearchEntry *e, gpointer) {
                     gtk_editable_set_text(GTK_EDITABLE(e), "");
                     search_emit(GTK_WIDGET(e), "cancelButtonPress");
                   }), nullptr);
  auto *focus = gtk_event_controller_focus_new();
  g_signal_connect_swapped(focus, "enter", G_CALLBACK(+[](GtkWidget *e) { search_emit(e, "searchFocus"); }),
                           entry);
  g_signal_connect_swapped(focus, "leave", G_CALLBACK(+[](GtkWidget *e) { search_emit(e, "searchBlur"); }),
                           entry);
  gtk_widget_add_controller(entry, focus);
  mark_all_headers();
  return entry;
}

void update_search_bar(GtkWidget *widget, const ShadowView &oldView, const ShadowView &view) {
  SearchBarData *d = search_of(widget);
  if (!d) return;
  d->emitter = std::static_pointer_cast<const ScreensEventEmitter>(view.eventEmitter);
  auto props = std::static_pointer_cast<const RawViewProps>(view.props);
  if (!props) return;
  std::string placeholder = props->string("placeholder");
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(widget), placeholder.c_str());
  if (!oldView.props && props->boolean("autoFocus")) {
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer w) -> gboolean {
      gtk_widget_grab_focus(GTK_WIDGET(w));
      return G_SOURCE_REMOVE;
    }, g_object_ref(widget), g_object_unref);
  }
  mark_all_headers();
}

void search_bar_command(GtkWidget *widget, const std::string &name, const folly::dynamic &args) {
  SearchBarData *d = search_of(widget);
  if (!d) return;
  if (name == "focus") {
    gtk_widget_grab_focus(widget);
  } else if (name == "blur") {
    if (GtkRoot *root = gtk_widget_get_root(widget)) gtk_root_set_focus(root, nullptr);
  } else if (name == "clearText" || name == "cancelSearch") {
    gtk_editable_set_text(GTK_EDITABLE(widget), "");
    if (name == "cancelSearch") {
      if (GtkRoot *root = gtk_widget_get_root(widget)) gtk_root_set_focus(root, nullptr);
      search_emit(widget, "cancelButtonPress");
    }
  } else if (name == "setText" && args.isArray() && !args.empty() && args[0].isString()) {
    d->settingText = true;
    gtk_editable_set_text(GTK_EDITABLE(widget), args[0].getString().c_str());
    d->settingText = false;
  }
}

}  // namespace rngtk_screens

std::shared_ptr<const rngtk::Package> rngtk_screens_package() {
  using namespace rngtk_screens;
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-screens";
  auto add = [&](rngtk::NativeComponent c) { package->components.push_back(std::move(c)); };

  rngtk::NativeComponent stack;
  stack.descriptor = concreteComponentDescriptorProvider<ScreenStackComponentDescriptor>();
  stack.create = create_stack;
  stack.update = update_stack;
  stack.insertChild = stack_insert;
  stack.removeChild = stack_remove;
  add(stack);

  // The host makes the screen's view (an RNView) and mounts its children.
  rngtk::NativeComponent screen;
  screen.descriptor = concreteComponentDescriptorProvider<ScreenComponentDescriptor>();
  screen.update = update_screen;
  add(screen);

  rngtk::NativeComponent config;
  config.descriptor = concreteComponentDescriptorProvider<HeaderConfig2ComponentDescriptor>();
  config.create = create_header_config;
  config.update = update_header_config;
  config.insertChild = header_config_insert;
  config.removeChild = header_config_remove;
  add(config);

  rngtk::NativeComponent subview;
  subview.descriptor = concreteComponentDescriptorProvider<HeaderSubviewComponentDescriptor>();
  subview.update = update_header_subview;
  add(subview);

  rngtk::NativeComponent search;
  search.descriptor = concreteComponentDescriptorProvider<SearchBarComponentDescriptor>();
  search.create = create_search_bar;
  search.update = update_search_bar;
  search.command = search_bar_command;
  add(search);

  for (auto descriptor : {concreteComponentDescriptorProvider<ScreenContainerComponentDescriptor>(),
                          concreteComponentDescriptorProvider<ContentWrapperComponentDescriptor>(),
                          concreteComponentDescriptorProvider<ScreenFooterComponentDescriptor>(),
                          concreteComponentDescriptorProvider<FullWindowOverlayComponentDescriptor>()}) {
    rngtk::NativeComponent plain;
    plain.descriptor = descriptor;
    add(plain);
  }
  return package;
}
