// react-native-svg's RNSVG* Fabric components on GTK4.
//
// RNSVGSvgView is a widget that draws its SVG document with librsvg. Every
// other RNSVG* component (G, Path, Rect, Text, LinearGradient, ...) is a
// node in its tree: a widget that's never shown, whose props and children
// the SvgView turns into the document (SvgDocument.cc). The host hands an
// SvgView or a node its children through NativeComponent's insertChild
// and removeChild, and any change redraws the SvgView it's in.
//
// The props are kept as react-native-svg's JS sends them (one generic props
// class for all of them, merged across updates), rather than a class per
// component from codegen: the document needs them as they are.
#include "SvgComponents.h"

#include "SvgDocument.h"

#include <librsvg/rsvg.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

#include <algorithm>
#include <map>
#include <mutex>

using namespace facebook::react;

namespace rngtk_svg {

// ---- Props ----------------------------------------------------------------

class SvgProps final : public ViewProps {
 public:
  SvgProps() = default;
  SvgProps(const PropsParserContext &context, const SvgProps &sourceProps,
           const RawProps &rawProps)
      : ViewProps(context, sourceProps, rawProps), svg(sourceProps.svg) {
    // Fabric sends what changed: merge it in (null is a value: "none").
    folly::dynamic changed = rawProps.toDynamic();
    if (changed.isObject()) {
      for (auto &[key, value] : changed.items()) svg[key] = value;
    }
  }

  folly::dynamic svg = folly::dynamic::object;
};

#define RNGTK_SVG_COMPONENTS(X)                                                              \
  X(SvgView) X(Group) X(Path) X(Rect) X(Circle) X(Ellipse) X(Line) X(Text) X(TSpan)          \
  X(TextPath) X(Use) X(Image) X(Defs) X(ClipPath) X(Mask) X(Pattern) X(Symbol) X(Marker)     \
  X(LinearGradient) X(RadialGradient) X(ForeignObject) X(Filter) X(FeGaussianBlur) X(FeOffset) \
  X(FeFlood) X(FeBlend) X(FeColorMatrix) X(FeComposite) X(FeMerge)

#define RNGTK_SVG_DECLARE(Name)                                                              \
  extern const char Name##ComponentName[] = "RNSVG" #Name;                                   \
  using Name##ShadowNode = ConcreteViewShadowNode<Name##ComponentName, SvgProps, ViewEventEmitter>; \
  using Name##ComponentDescriptor = ConcreteComponentDescriptor<Name##ShadowNode>;
RNGTK_SVG_COMPONENTS(RNGTK_SVG_DECLARE)
#undef RNGTK_SVG_DECLARE

// ---- Widgets ----------------------------------------------------------------

namespace {

constexpr const char *kNode = "rngtk-svg-node";

Node *nodeOf(GtkWidget *widget) {
  return static_cast<Node *>(g_object_get_data(G_OBJECT(widget), kNode));
}

// The SvgView's state: its tree's root, and the document drawn last.
struct View {
  Node *root = nullptr;
  GtkWidget *widget = nullptr;
  RsvgHandle *handle = nullptr;
  std::string document;
  int width = -1, height = -1;
  bool dirty = true;
  // RNGTK_SVG_STATS
  gint64 statsStart = 0, lastFrame = 0, maxGap = 0;
  int frames = 0;
  ~View() {
    if (handle) g_object_unref(handle);
  }
};

// The tag → SvgView map, for RNSVGSvgViewModule.toDataURL.
std::mutex viewsMutex;
std::map<int, GtkWidget *> views;

}  // namespace

// rngtk_svg_view: an SvgView's widget. It draws with librsvg in snapshot.
#define RNGTK_TYPE_SVG_VIEW (rngtk_svg_view_get_type())
G_DECLARE_FINAL_TYPE(RngtkSvgView, rngtk_svg_view, RNGTK, SVG_VIEW, GtkWidget)

struct _RngtkSvgView {
  GtkWidget parent_instance;
  View *view;
};

G_DEFINE_FINAL_TYPE(RngtkSvgView, rngtk_svg_view, GTK_TYPE_WIDGET)

// RNGTK_SVG_STATS=1: each SvgView's redraws per second and its longest gap
// between two, printed once a second while it animates.
static void frameStats(View *v) {
  static const bool on = g_getenv("RNGTK_SVG_STATS") != nullptr;
  if (!on) return;
  gint64 now = g_get_monotonic_time();
  if (v->statsStart == 0) v->statsStart = now;
  if (v->lastFrame) v->maxGap = std::max(v->maxGap, now - v->lastFrame);
  v->lastFrame = now;
  v->frames++;
  if (now - v->statsStart >= G_USEC_PER_SEC) {
    g_printerr("svg-stats view=%p frames=%d max-gap-ms=%.1f\n", static_cast<void *>(v), v->frames,
               v->maxGap / 1000.0);
    v->statsStart = now;
    v->frames = 0;
    v->maxGap = 0;
  }
}

static void rngtk_svg_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  View *v = RNGTK_SVG_VIEW(widget)->view;
  frameStats(v);
  int w = gtk_widget_get_width(widget);
  int h = gtk_widget_get_height(widget);
  if (!v || !v->root || w <= 0 || h <= 0) return;
  if (v->dirty || w != v->width || h != v->height) {
    v->width = w;
    v->height = h;
    v->dirty = false;
    std::string document = svgDocument(*v->root, w, h);
    if (document != v->document || !v->handle) {
      v->document = std::move(document);
      // RNGTK_SVG_DEBUG=1 prints each document librsvg gets.
      static const bool debug = g_getenv("RNGTK_SVG_DEBUG") != nullptr;
      if (debug) g_printerr("react-native-svg document:\n%s\n", v->document.c_str());
      if (v->handle) g_object_unref(v->handle);
      GError *error = nullptr;
      v->handle = rsvg_handle_new_from_data(reinterpret_cast<const guint8 *>(v->document.data()),
                                            v->document.size(), &error);
      if (!v->handle) {
        g_warning("react-native-svg: librsvg can't read the document: %s", error ? error->message : "?");
        g_clear_error(&error);
        return;
      }
    }
  }
  if (!v->handle) return;
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
  cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &bounds);
  RsvgRectangle viewport = {0, 0, double(w), double(h)};
  GError *error = nullptr;
  if (!rsvg_handle_render_document(v->handle, cr, &viewport, &error)) {
    g_warning("react-native-svg: %s", error ? error->message : "librsvg couldn't draw");
    g_clear_error(&error);
  }
  cairo_destroy(cr);
}

static void rngtk_svg_view_finalize(GObject *object) {
  delete RNGTK_SVG_VIEW(object)->view;
  G_OBJECT_CLASS(rngtk_svg_view_parent_class)->finalize(object);
}

static void rngtk_svg_view_class_init(RngtkSvgViewClass *klass) {
  GTK_WIDGET_CLASS(klass)->snapshot = rngtk_svg_view_snapshot;
  G_OBJECT_CLASS(klass)->finalize = rngtk_svg_view_finalize;
}

static void rngtk_svg_view_init(RngtkSvgView *self) {
  self->view = new View();
  self->view->widget = GTK_WIDGET(self);
}

// The node → widget link both ways: a Node knows its widget through this
// map (nodes are plain structs; widgets own them).
namespace {
std::map<const Node *, GtkWidget *> widgets;

void redraw(Node *node) {
  while (node && node->parent) node = node->parent;
  if (!node) return;
  auto it = widgets.find(node);
  if (it == widgets.end() || !RNGTK_IS_SVG_VIEW(it->second)) return;
  RNGTK_SVG_VIEW(it->second)->view->dirty = true;
  gtk_widget_queue_draw(it->second);
}

GtkWidget *createNode(const ShadowView &view) {
  bool root = std::string(view.componentName) == "RNSVGSvgView";
  GtkWidget *widget = root ? GTK_WIDGET(g_object_new(RNGTK_TYPE_SVG_VIEW, nullptr))
                           : gtk_label_new(nullptr);  // never shown: a node
  auto *node = new Node();
  node->component = view.componentName;
  g_object_set_data_full(G_OBJECT(widget), kNode, node, [](gpointer p) {
    auto *n = static_cast<Node *>(p);
    widgets.erase(n);
    for (Node *child : n->children) child->parent = nullptr;
    if (n->parent) {
      auto &siblings = n->parent->children;
      siblings.erase(std::remove(siblings.begin(), siblings.end(), n), siblings.end());
    }
    delete n;
  });
  widgets[node] = widget;
  if (root) {
    RNGTK_SVG_VIEW(widget)->view->root = node;
    std::lock_guard<std::mutex> lock(viewsMutex);
    views[view.tag] = widget;
    g_object_set_data_full(G_OBJECT(widget), "rngtk-svg-tag", GINT_TO_POINTER(view.tag), [](gpointer tag) {
      std::lock_guard<std::mutex> lock(viewsMutex);
      views.erase(GPOINTER_TO_INT(tag));
    });
  }
  return widget;
}

void updateNode(GtkWidget *widget, const ShadowView &, const ShadowView &newView) {
  Node *node = nodeOf(widget);
  auto props = std::static_pointer_cast<const SvgProps>(newView.props);
  if (!node || !props) return;
  node->props = props->svg;
  redraw(node);
}

void insertChild(GtkWidget *parent, GtkWidget *child, int index) {
  Node *p = nodeOf(parent);
  Node *c = nodeOf(child);
  if (!p || !c) return;
  if (c->parent) {
    auto &old = c->parent->children;
    old.erase(std::remove(old.begin(), old.end(), c), old.end());
  }
  c->parent = p;
  index = std::clamp(index, 0, int(p->children.size()));
  p->children.insert(p->children.begin() + index, c);
  redraw(p);
}

void removeChild(GtkWidget *parent, GtkWidget *child) {
  Node *p = nodeOf(parent);
  Node *c = nodeOf(child);
  if (!p || !c) return;
  p->children.erase(std::remove(p->children.begin(), p->children.end(), c), p->children.end());
  c->parent = nullptr;
  redraw(p);
}

template <typename Descriptor>
rngtk::NativeComponent component() {
  rngtk::NativeComponent c;
  c.descriptor = concreteComponentDescriptorProvider<Descriptor>();
  c.create = createNode;
  c.update = updateNode;
  c.insertChild = insertChild;
  c.removeChild = removeChild;
  return c;
}

}  // namespace

std::vector<rngtk::NativeComponent> svgComponents() {
  std::vector<rngtk::NativeComponent> out;
#define RNGTK_SVG_COMPONENT(Name) out.push_back(component<Name##ComponentDescriptor>());
  RNGTK_SVG_COMPONENTS(RNGTK_SVG_COMPONENT)
#undef RNGTK_SVG_COMPONENT
  return out;
}

std::string documentForTag(int tag, double *width, double *height) {
  GtkWidget *widget = nullptr;
  {
    std::lock_guard<std::mutex> lock(viewsMutex);
    auto it = views.find(tag);
    if (it == views.end()) return "";
    widget = it->second;
  }
  View *v = RNGTK_SVG_VIEW(widget)->view;
  *width = gtk_widget_get_width(widget);
  *height = gtk_widget_get_height(widget);
  return v->root ? svgDocument(*v->root, *width, *height) : "";
}

}  // namespace rngtk_svg
