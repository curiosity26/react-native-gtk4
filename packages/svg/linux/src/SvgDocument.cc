#include "SvgDocument.h"

#include <glib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace rngtk_svg {

namespace {

std::string escape(const std::string &s) {
  gchar *e = g_markup_escape_text(s.c_str(), -1);
  std::string out = e;
  g_free(e);
  return out;
}

std::string number(double v) {
  char buf[32];
  g_ascii_formatd(buf, sizeof(buf), "%.6g", v);
  return buf;
}

// A length or number prop: a number, or a string ("50%", "1em").
std::string length(const folly::dynamic &v) {
  if (v.isNumber()) return number(v.asDouble());
  if (v.isString()) return v.asString();
  return "";
}

// A length list: an array of lengths, or one.
std::string lengthList(const folly::dynamic &v) {
  if (!v.isArray()) return length(v);
  std::string out;
  for (const auto &item : v) {
    std::string l = length(item);
    if (l.empty()) continue;
    if (!out.empty()) out += ' ';
    out += l;
  }
  return out;
}

const folly::dynamic *prop(const Node &node, const char *name) {
  const folly::dynamic *v = node.props.get_ptr(name);
  return v && !v->isNull() ? v : nullptr;
}

bool inPropList(const Node &node, const char *name) {
  const folly::dynamic *list = node.props.get_ptr("propList");
  if (!list || !list->isArray()) return false;
  for (const auto &item : *list) {
    if (item.isString() && item.asString() == name) return true;
  }
  return false;
}

std::string units(const folly::dynamic &v) {
  if (v.isString()) return v.asString();
  if (v.isNumber()) return v.asInt() == 1 ? "userSpaceOnUse" : "objectBoundingBox";
  return "";
}

class Writer {
 public:
  std::ostringstream out;

  void attr(const char *name, const std::string &value) {
    if (value.empty()) return;
    out << ' ' << name << "=\"" << escape(value) << '"';
  }

  void lengthAttr(const Node &node, const char *propName, const char *attrName = nullptr) {
    if (const auto *v = prop(node, propName)) attr(attrName ? attrName : propName, length(*v));
  }

  void listAttr(const Node &node, const char *propName, const char *attrName = nullptr) {
    if (const auto *v = prop(node, propName)) attr(attrName ? attrName : propName, lengthList(*v));
  }

  void stringAttr(const Node &node, const char *propName, const char *attrName = nullptr) {
    if (const auto *v = prop(node, propName); v && v->isString()) {
      attr(attrName ? attrName : propName, v->asString());
    }
  }

  void urlAttr(const Node &node, const char *propName, const char *attrName) {
    if (const auto *v = prop(node, propName); v && v->isString() && !v->asString().empty()) {
      attr(attrName, "url(#" + v->asString() + ")");
    }
  }

  void matrixAttr(const Node &node, const char *propName, const char *attrName) {
    const auto *m = prop(node, propName);
    if (!m || !m->isArray() || m->size() != 6) return;
    std::string s = "matrix(";
    for (size_t i = 0; i < 6; i++) {
      if (i) s += ' ';
      s += number((*m)[i].isNumber() ? (*m)[i].asDouble() : 0);
    }
    attr(attrName, s + ")");
  }

  // viewBox and preserveAspectRatio (Svg, Symbol, Pattern, Marker, Image).
  void viewBox(const Node &node) {
    const auto *w = prop(node, "vbWidth");
    const auto *h = prop(node, "vbHeight");
    if (w && h && w->isNumber() && h->isNumber() && (w->asDouble() > 0 || h->asDouble() > 0)) {
      double x = prop(node, "minX") && prop(node, "minX")->isNumber() ? prop(node, "minX")->asDouble() : 0;
      double y = prop(node, "minY") && prop(node, "minY")->isNumber() ? prop(node, "minY")->asDouble() : 0;
      attr("viewBox", number(x) + " " + number(y) + " " + number(w->asDouble()) + " " + number(h->asDouble()));
    }
    aspect(node);
  }

  void aspect(const Node &node) {
    const auto *align = prop(node, "align");
    const auto *meet = prop(node, "meetOrSlice");
    int m = meet && meet->isNumber() ? int(meet->asInt()) : 0;
    if (m == 2) {
      attr("preserveAspectRatio", "none");
    } else if (align && align->isString()) {
      attr("preserveAspectRatio", align->asString() + (m == 1 ? " slice" : " meet"));
    }
  }

  // fill and stroke: {type: 0, payload: color} | {type: 1, brushRef} |
  // {type: 2} (currentColor) | 3, 4 (context-fill, -stroke) | null (none).
  void brush(const Node &node, const char *propName) {
    if (!inPropList(node, propName)) return;
    const folly::dynamic *v = node.props.get_ptr(propName);
    if (!v || v->isNull()) {
      attr(propName, "none");
      return;
    }
    if (v->isNumber()) {
      attr(propName, cssColor(*v));
      return;
    }
    if (!v->isObject()) return;
    int type = v->get_ptr("type") && (*v)["type"].isNumber() ? int((*v)["type"].asInt()) : 0;
    switch (type) {
      case 0:
        if (const auto *payload = v->get_ptr("payload")) {
          std::string c = cssColor(*payload);
          attr(propName, c.empty() ? "black" : c);
        }
        break;
      case 1:
        if (const auto *ref = v->get_ptr("brushRef"); ref && ref->isString()) {
          attr(propName, "url(#" + ref->asString() + ")");
        }
        break;
      case 2: attr(propName, "currentColor"); break;
      case 3: attr(propName, "context-fill"); break;
      case 4: attr(propName, "context-stroke"); break;
    }
  }

  // What every element can have: id, opacity, transform, clip, mask,
  // markers, filter, display.
  void common(const Node &node) {
    stringAttr(node, "name", "id");
    if (const auto *o = prop(node, "opacity"); o && o->isNumber() && o->asDouble() != 1) {
      attr("opacity", number(o->asDouble()));
    }
    matrixAttr(node, "matrix", "transform");
    urlAttr(node, "clipPath", "clip-path");
    if (prop(node, "clipPath")) {
      if (const auto *r = prop(node, "clipRule"); r && r->isNumber()) {
        attr("clip-rule", r->asInt() == 0 ? "evenodd" : "nonzero");
      }
    }
    urlAttr(node, "mask", "mask");
    urlAttr(node, "markerStart", "marker-start");
    urlAttr(node, "markerMid", "marker-mid");
    urlAttr(node, "markerEnd", "marker-end");
    urlAttr(node, "filter", "filter");
    stringAttr(node, "display");
  }

  // Fill and stroke, only where the JS set them (propList): the rest
  // inherits, as in SVG.
  void paint(const Node &node) {
    if (const auto *c = prop(node, "color")) attr("color", cssColor(*c));
    brush(node, "fill");
    if (inPropList(node, "fillOpacity")) lengthAttr(node, "fillOpacity", "fill-opacity");
    if (inPropList(node, "fillRule")) {
      if (const auto *r = prop(node, "fillRule"); r && r->isNumber()) {
        attr("fill-rule", r->asInt() == 0 ? "evenodd" : "nonzero");
      }
    }
    brush(node, "stroke");
    if (inPropList(node, "strokeOpacity")) lengthAttr(node, "strokeOpacity", "stroke-opacity");
    if (inPropList(node, "strokeWidth")) lengthAttr(node, "strokeWidth", "stroke-width");
    if (inPropList(node, "strokeLinecap")) {
      if (const auto *v = prop(node, "strokeLinecap"); v && v->isNumber()) {
        static const char *caps[] = {"butt", "round", "square"};
        attr("stroke-linecap", caps[std::clamp<int64_t>(v->asInt(), 0, 2)]);
      }
    }
    if (inPropList(node, "strokeLinejoin")) {
      if (const auto *v = prop(node, "strokeLinejoin"); v && v->isNumber()) {
        static const char *joins[] = {"miter", "round", "bevel"};
        attr("stroke-linejoin", joins[std::clamp<int64_t>(v->asInt(), 0, 2)]);
      }
    }
    if (inPropList(node, "strokeDasharray")) {
      const auto *v = prop(node, "strokeDasharray");
      attr("stroke-dasharray", v ? lengthList(*v) : "none");
    }
    if (inPropList(node, "strokeDashoffset")) lengthAttr(node, "strokeDashoffset", "stroke-dashoffset");
    if (inPropList(node, "strokeMiterlimit")) lengthAttr(node, "strokeMiterlimit", "stroke-miterlimit");
    if (const auto *v = prop(node, "vectorEffect"); v && v->isNumber() && v->asInt() == 1) {
      attr("vector-effect", "non-scaling-stroke");
    }
  }

  // Text's font: an object of CSS-ish font props.
  void font(const Node &node) {
    const auto *f = prop(node, "font");
    if (!f || !f->isObject()) return;
    static const std::pair<const char *, const char *> names[] = {
        {"fontSize", "font-size"},         {"fontFamily", "font-family"},
        {"fontWeight", "font-weight"},     {"fontStyle", "font-style"},
        {"fontVariant", "font-variant"},   {"fontStretch", "font-stretch"},
        {"textAnchor", "text-anchor"},     {"textDecoration", "text-decoration"},
        {"letterSpacing", "letter-spacing"}, {"wordSpacing", "word-spacing"},
        {"kerning", "kerning"},
    };
    for (const auto &[key, attrName] : names) {
      if (const auto *v = f->get_ptr(key); v && !v->isNull()) attr(attrName, length(*v));
    }
  }

  void text(const Node &node) {
    font(node);
    listAttr(node, "x");
    listAttr(node, "y");
    listAttr(node, "dx");
    listAttr(node, "dy");
    listAttr(node, "rotate");
    lengthAttr(node, "textLength");
    stringAttr(node, "lengthAdjust");
    lengthAttr(node, "baselineShift", "baseline-shift");
    stringAttr(node, "alignmentBaseline", "dominant-baseline");
  }

  void gradientStops(const Node &node) {
    const auto *g = prop(node, "gradient");
    if (!g || !g->isArray()) return;
    for (size_t i = 0; i + 1 < g->size(); i += 2) {
      const auto &offset = (*g)[i];
      const auto &color = (*g)[i + 1];
      if (!offset.isNumber() || !color.isNumber()) continue;
      uint32_t argb = uint32_t(int64_t(color.asDouble()));
      char c[16];
      std::snprintf(c, sizeof(c), "#%06x", argb & 0xffffff);
      out << "<stop offset=\"" << number(offset.asDouble()) << "\" stop-color=\"" << c
          << "\" stop-opacity=\"" << number(((argb >> 24) & 0xff) / 255.0) << "\"/>";
    }
  }

  void element(const Node &node);

  void children(const Node &node) {
    for (const Node *child : node.children) element(*child);
  }
};

// The element for a component, and what it takes beyond the common
// attributes. Unknown ones (ForeignObject) become a group.
void Writer::element(const Node &node) {
  const std::string &c = node.component;
  std::string tag;
  if (c == "RNSVGGroup" || c == "RNSVGForeignObject") tag = "g";
  else if (c == "RNSVGPath") tag = "path";
  else if (c == "RNSVGRect") tag = "rect";
  else if (c == "RNSVGCircle") tag = "circle";
  else if (c == "RNSVGEllipse") tag = "ellipse";
  else if (c == "RNSVGLine") tag = "line";
  else if (c == "RNSVGText") tag = "text";
  else if (c == "RNSVGTSpan") tag = "tspan";
  else if (c == "RNSVGTextPath") tag = "textPath";
  else if (c == "RNSVGUse") tag = "use";
  else if (c == "RNSVGImage") tag = "image";
  else if (c == "RNSVGDefs") tag = "defs";
  else if (c == "RNSVGClipPath") tag = "clipPath";
  else if (c == "RNSVGMask") tag = "mask";
  else if (c == "RNSVGPattern") tag = "pattern";
  else if (c == "RNSVGSymbol") tag = "symbol";
  else if (c == "RNSVGMarker") tag = "marker";
  else if (c == "RNSVGLinearGradient") tag = "linearGradient";
  else if (c == "RNSVGRadialGradient") tag = "radialGradient";
  else if (c == "RNSVGFilter") tag = "filter";
  else if (c == "RNSVGFeGaussianBlur") tag = "feGaussianBlur";
  else if (c == "RNSVGFeOffset") tag = "feOffset";
  else if (c == "RNSVGFeFlood") tag = "feFlood";
  else if (c == "RNSVGFeBlend") tag = "feBlend";
  else if (c == "RNSVGFeColorMatrix") tag = "feColorMatrix";
  else if (c == "RNSVGFeComposite") tag = "feComposite";
  else if (c == "RNSVGFeMerge") tag = "feMerge";
  else tag = "g";

  out << '<' << tag;
  bool primitive = tag.rfind("fe", 0) == 0;
  if (!primitive) common(node);
  if (tag == "g" || tag == "path" || tag == "rect" || tag == "circle" || tag == "ellipse" ||
      tag == "line" || tag == "text" || tag == "tspan" || tag == "textPath" || tag == "use") {
    paint(node);
  }
  if (tag == "g") font(node);
  if (tag == "path") stringAttr(node, "d");
  if (tag == "rect") {
    for (const char *p : {"x", "y", "width", "height", "rx", "ry"}) lengthAttr(node, p);
  }
  if (tag == "circle") {
    for (const char *p : {"cx", "cy", "r"}) lengthAttr(node, p);
  }
  if (tag == "ellipse") {
    for (const char *p : {"cx", "cy", "rx", "ry"}) lengthAttr(node, p);
  }
  if (tag == "line") {
    for (const char *p : {"x1", "y1", "x2", "y2"}) lengthAttr(node, p);
  }
  if (tag == "text" || tag == "tspan" || tag == "textPath") text(node);
  // Text keeps its spaces ("Hello " then a TSpan): React already decided them.
  if (tag == "text") attr("xml:space", "preserve");
  if (tag == "textPath") {
    if (const auto *h = prop(node, "href"); h && h->isString()) attr("href", "#" + h->asString());
    lengthAttr(node, "startOffset");
    stringAttr(node, "method");
    stringAttr(node, "spacing");
    stringAttr(node, "side");
  }
  if (tag == "use") {
    if (const auto *h = prop(node, "href"); h && h->isString()) attr("href", "#" + h->asString());
    lengthAttr(node, "x");
    lengthAttr(node, "y");
    // react-native-svg sends 0 for no size, which in SVG means "don't draw".
    for (const char *p : {"width", "height"}) {
      if (const auto *v = prop(node, p); v && length(*v) != "0") attr(p, length(*v));
    }
  }
  if (tag == "image") {
    for (const char *p : {"x", "y", "width", "height"}) lengthAttr(node, p);
    // src: an image source ({uri}); librsvg loads file: and data: URIs.
    if (const auto *src = prop(node, "src")) {
      const folly::dynamic *uri = src->isObject() ? src->get_ptr("uri") : src;
      if (uri && uri->isString()) attr("href", uri->asString());
    }
    aspect(node);
  }
  if (tag == "mask") {
    for (const char *p : {"x", "y", "width", "height"}) lengthAttr(node, p);
    if (const auto *u = prop(node, "maskUnits")) attr("maskUnits", units(*u));
    if (const auto *u = prop(node, "maskContentUnits")) attr("maskContentUnits", units(*u));
    if (const auto *t = prop(node, "maskType"); t && t->isNumber()) {
      attr("mask-type", t->asInt() == 1 ? "alpha" : "luminance");
    }
  }
  if (tag == "pattern") {
    for (const char *p : {"x", "y", "width", "height"}) lengthAttr(node, p);
    if (const auto *u = prop(node, "patternUnits")) attr("patternUnits", units(*u));
    if (const auto *u = prop(node, "patternContentUnits")) attr("patternContentUnits", units(*u));
    matrixAttr(node, "patternTransform", "patternTransform");
    viewBox(node);
  }
  if (tag == "symbol") viewBox(node);
  if (tag == "marker") {
    for (const char *p : {"refX", "refY", "markerWidth", "markerHeight"}) lengthAttr(node, p);
    stringAttr(node, "markerUnits");
    if (const auto *o = prop(node, "orient")) attr("orient", length(*o));
    viewBox(node);
  }
  if (tag == "linearGradient" || tag == "radialGradient") {
    if (const auto *u = prop(node, "gradientUnits")) attr("gradientUnits", units(*u));
    matrixAttr(node, "gradientTransform", "gradientTransform");
    if (tag == "linearGradient") {
      for (const char *p : {"x1", "y1", "x2", "y2"}) lengthAttr(node, p);
    } else {
      for (const char *p : {"fx", "fy", "cx", "cy"}) lengthAttr(node, p);
      lengthAttr(node, "rx", "r");
    }
  }
  if (tag == "filter") {
    for (const char *p : {"x", "y", "width", "height"}) lengthAttr(node, p);
    if (const auto *u = prop(node, "filterUnits")) attr("filterUnits", units(*u));
    if (const auto *u = prop(node, "primitiveUnits")) attr("primitiveUnits", units(*u));
  }
  if (primitive) {
    for (const char *p : {"x", "y", "width", "height"}) lengthAttr(node, p);
    stringAttr(node, "result");
    stringAttr(node, "in1", "in");
    stringAttr(node, "in2");
  }
  if (tag == "feGaussianBlur") {
    std::string x = prop(node, "stdDeviationX") ? length(*prop(node, "stdDeviationX")) : "0";
    std::string y = prop(node, "stdDeviationY") ? length(*prop(node, "stdDeviationY")) : x;
    attr("stdDeviation", x + " " + y);
    stringAttr(node, "edgeMode");
  }
  if (tag == "feOffset") {
    lengthAttr(node, "dx");
    lengthAttr(node, "dy");
  }
  if (tag == "feFlood") {
    if (const auto *c = prop(node, "floodColor")) {
      const folly::dynamic *payload = c->isObject() ? c->get_ptr("payload") : c;
      if (payload) attr("flood-color", cssColor(*payload));
    }
    lengthAttr(node, "floodOpacity", "flood-opacity");
  }
  if (tag == "feBlend") stringAttr(node, "mode");
  if (tag == "feColorMatrix") {
    stringAttr(node, "type");
    listAttr(node, "values");
  }
  if (tag == "feComposite") {
    stringAttr(node, "operator1", "operator");
    for (const char *p : {"k1", "k2", "k3", "k4"}) lengthAttr(node, p);
  }

  // Contents: a TSpan's text, a gradient's stops, FeMerge's nodes.
  std::string content;
  if (const auto *t = prop(node, "content"); t && t->isString()) content = t->asString();
  bool gradient = tag == "linearGradient" || tag == "radialGradient";
  const auto *nodes = tag == "feMerge" ? prop(node, "nodes") : nullptr;
  if (node.children.empty() && content.empty() && !gradient && !(nodes && nodes->isArray())) {
    out << "/>";
    return;
  }
  out << '>' << escape(content);
  if (gradient) gradientStops(node);
  if (nodes && nodes->isArray()) {
    for (const auto &n : *nodes) {
      out << "<feMergeNode";
      if (n.isString()) attr("in", n.asString());
      out << "/>";
    }
  }
  children(node);
  out << "</" << tag << '>';
}

}  // namespace

std::string cssColor(const folly::dynamic &color) {
  if (!color.isNumber()) return "";
  uint32_t argb = uint32_t(int64_t(color.asDouble()));
  char buf[48];
  std::snprintf(buf, sizeof(buf), "rgba(%u,%u,%u,%s)", (argb >> 16) & 0xff, (argb >> 8) & 0xff,
                argb & 0xff, number(((argb >> 24) & 0xff) / 255.0).c_str());
  return buf;
}

std::string svgDocument(const Node &root, double width, double height) {
  Writer w;
  w.out << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\"";
  w.attr("width", number(width));
  w.attr("height", number(height));
  w.viewBox(root);
  if (const auto *c = prop(root, "color")) w.attr("color", cssColor(*c));
  if (const auto *c = prop(root, "tintColor"); c && !prop(root, "color")) w.attr("color", cssColor(*c));
  w.out << '>';
  w.children(root);
  w.out << "</svg>";
  return w.out.str();
}

}  // namespace rngtk_svg
