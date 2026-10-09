#include "GtkViewProps.h"

#include "rn_view.h"

#include <react/renderer/graphics/BackgroundImage.h>

#include <cmath>
#include <variant>
#include <vector>

using namespace facebook::react;

namespace rngtk {

namespace {

RNLineStyle line_style(BorderStyle style) {
  switch (style) {
    case BorderStyle::Dashed:
      return RN_LINE_DASHED;
    case BorderStyle::Dotted:
      return RN_LINE_DOTTED;
    default:
      return RN_LINE_SOLID;
  }
}

RNLineStyle line_style(OutlineStyle style) {
  switch (style) {
    case OutlineStyle::Dashed:
      return RN_LINE_DASHED;
    case OutlineStyle::Dotted:
      return RN_LINE_DOTTED;
    default:
      return RN_LINE_SOLID;
  }
}

graphene_size_t radius(const CornerRadii &r) {
  graphene_size_t s;
  graphene_size_init(&s, r.horizontal, r.vertical);
  return s;
}

RNViewStyle view_style(const ViewProps &props, const LayoutMetrics &layout) {
  BorderMetrics border = props.resolveBorderMetrics(layout);
  RNViewStyle s{};
  s.background = to_rgba(props.backgroundColor);
  s.radii[0] = radius(border.borderRadii.topLeft);
  s.radii[1] = radius(border.borderRadii.topRight);
  s.radii[2] = radius(border.borderRadii.bottomRight);
  s.radii[3] = radius(border.borderRadii.bottomLeft);
  const auto &w = border.borderWidths;
  const float widths[4] = {w.top, w.right, w.bottom, w.left};
  const auto &c = border.borderColors;
  const SharedColor colors[4] = {c.top, c.right, c.bottom, c.left};
  for (int i = 0; i < 4; i++) {
    s.border_widths[i] = widths[i];
    // RN's default border color is black.
    s.border_colors[i] = colors[i] ? to_rgba(colors[i]) : GdkRGBA{0, 0, 0, 1};
  }
  s.border_style = line_style(border.borderStyles.top);
  s.outline_width = props.outlineWidth;
  s.outline_offset = props.outlineOffset;
  s.outline_color =
      props.outlineColor ? to_rgba(props.outlineColor) : GdkRGBA{0, 0, 0, 1};
  s.outline_style = line_style(props.outlineStyle);
  s.clip_children = props.getClipsContentToBounds();
  s.backface_hidden = props.backfaceVisibility == BackfaceVisibility::Hidden;
  return s;
}

std::vector<RNBoxShadow> box_shadows(const ViewProps &props) {
  std::vector<RNBoxShadow> out;
  for (const auto &b : props.boxShadow) {
    out.push_back(RNBoxShadow{b.offsetX, b.offsetY, b.blurRadius,
                              b.spreadDistance, to_rgba(b.color), b.inset});
  }
  // Legacy iOS shadow props: an outset shadow of the view's box (iOS shades
  // the layer's alpha; for a view with a background that's the same).
  if (props.boxShadow.empty() && props.shadowColor &&
      props.shadowOpacity > 0) {
    GdkRGBA color = to_rgba(props.shadowColor);
    color.alpha *= std::min(1.0f, float(props.shadowOpacity));
    out.push_back(RNBoxShadow{props.shadowOffset.width,
                              props.shadowOffset.height, props.shadowRadius, 0,
                              color, FALSE});
  }
  return out;
}

std::vector<RNFilter> filters(const ViewProps &props) {
  std::vector<RNFilter> out;
  for (const auto &f : props.filter) {
    RNFilter r{};
    if (const auto *amount = std::get_if<Float>(&f.parameters)) {
      r.amount = *amount;
    }
    switch (f.type) {
      case FilterType::Blur: r.type = RN_FILTER_BLUR; break;
      case FilterType::Brightness: r.type = RN_FILTER_BRIGHTNESS; break;
      case FilterType::Contrast: r.type = RN_FILTER_CONTRAST; break;
      case FilterType::Grayscale: r.type = RN_FILTER_GRAYSCALE; break;
      case FilterType::HueRotate: r.type = RN_FILTER_HUE_ROTATE; break;
      case FilterType::Invert: r.type = RN_FILTER_INVERT; break;
      case FilterType::Opacity: r.type = RN_FILTER_OPACITY; break;
      case FilterType::Saturate: r.type = RN_FILTER_SATURATE; break;
      case FilterType::Sepia: r.type = RN_FILTER_SEPIA; break;
      case FilterType::DropShadow: {
        r.type = RN_FILTER_DROP_SHADOW;
        if (const auto *p = std::get_if<DropShadowParams>(&f.parameters)) {
          r.shadow = RNBoxShadow{p->offsetX, p->offsetY, p->standardDeviation,
                                 0, to_rgba(p->color), FALSE};
        }
        break;
      }
    }
    out.push_back(r);
  }
  return out;
}

RNColorStop color_stop(const ColorStop &stop) {
  RNColorStop s{to_rgba(stop.color), stop.position.value, RN_STOP_AUTO};
  if (stop.position.unit == UnitType::Percent) s.unit = RN_STOP_PERCENT;
  if (stop.position.unit == UnitType::Point) s.unit = RN_STOP_POINTS;
  return s;
}

// CSS angle for "to <corner>": perpendicular to the other diagonal.
float keyword_angle(GradientKeyword keyword, float w, float h) {
  float a = std::atan2(h, w) * 180 / float(M_PI);
  switch (keyword) {
    case GradientKeyword::ToTopRight:
      return a;
    case GradientKeyword::ToBottomRight:
      return 180 - a;
    case GradientKeyword::ToBottomLeft:
      return 180 + a;
    case GradientKeyword::ToTopLeft:
      return 360 - a;
  }
  return 180;
}

float position_fraction(const std::optional<ValueUnit> &near,
                        const std::optional<ValueUnit> &far, float size) {
  if (near && size > 0) return near->resolve(size) / size;
  if (far && size > 0) return 1 - far->resolve(size) / size;
  return 0.5f;
}

void apply_gradients(RNView *view, const ViewProps &props, float w, float h) {
  std::vector<std::vector<RNColorStop>> stops;
  std::vector<RNGradient> gradients;
  stops.reserve(props.backgroundImage.size());
  for (const auto &image : props.backgroundImage) {
    RNGradient g{};
    const std::vector<ColorStop> *colorStops = nullptr;
    if (const auto *linear = std::get_if<LinearGradient>(&image)) {
      if (const auto *angle = std::get_if<Float>(&linear->direction)) {
        g.angle = *angle;
      } else {
        g.angle = keyword_angle(std::get<GradientKeyword>(linear->direction),
                                w, h);
      }
      colorStops = &linear->colorStops;
    } else if (const auto *radial = std::get_if<RadialGradient>(&image)) {
      g.radial = TRUE;
      g.circle = radial->shape == RadialGradientShape::Circle;
      g.center_x = position_fraction(radial->position.left,
                                     radial->position.right, w);
      g.center_y = position_fraction(radial->position.top,
                                     radial->position.bottom, h);
      colorStops = &radial->colorStops;
    }
    if (!colorStops) continue;
    auto &list = stops.emplace_back();
    for (const auto &stop : *colorStops) list.push_back(color_stop(stop));
    g.stops = list.data();
    g.n_stops = list.size();
    gradients.push_back(g);
  }
  rn_view_set_background_gradients(view, gradients.data(), gradients.size());
}

const char *cursor_name(Cursor cursor) {
  switch (cursor) {
    case Cursor::Auto: return nullptr;
    case Cursor::Alias: return "alias";
    case Cursor::AllScroll: return "all-scroll";
    case Cursor::Cell: return "cell";
    case Cursor::ColResize: return "col-resize";
    case Cursor::ContextMenu: return "context-menu";
    case Cursor::Copy: return "copy";
    case Cursor::Crosshair: return "crosshair";
    case Cursor::Default: return "default";
    case Cursor::EResize: return "e-resize";
    case Cursor::EWResize: return "ew-resize";
    case Cursor::Grab: return "grab";
    case Cursor::Grabbing: return "grabbing";
    case Cursor::Help: return "help";
    case Cursor::Move: return "move";
    case Cursor::NEResize: return "ne-resize";
    case Cursor::NESWResize: return "nesw-resize";
    case Cursor::NResize: return "n-resize";
    case Cursor::NSResize: return "ns-resize";
    case Cursor::NWResize: return "nw-resize";
    case Cursor::NWSEResize: return "nwse-resize";
    case Cursor::NoDrop: return "no-drop";
    case Cursor::None: return "none";
    case Cursor::NotAllowed: return "not-allowed";
    case Cursor::Pointer: return "pointer";
    case Cursor::Progress: return "progress";
    case Cursor::RowResize: return "row-resize";
    case Cursor::SResize: return "s-resize";
    case Cursor::SEResize: return "se-resize";
    case Cursor::SWResize: return "sw-resize";
    case Cursor::Text: return "text";
    case Cursor::Url: return "pointer";
    case Cursor::WResize: return "w-resize";
    case Cursor::Wait: return "wait";
    case Cursor::ZoomIn: return "zoom-in";
    case Cursor::ZoomOut: return "zoom-out";
  }
  return nullptr;
}

RNPointerEvents pointer_events(PointerEventsMode mode) {
  switch (mode) {
    case PointerEventsMode::None: return RN_POINTER_EVENTS_NONE;
    case PointerEventsMode::BoxNone: return RN_POINTER_EVENTS_BOX_NONE;
    case PointerEventsMode::BoxOnly: return RN_POINTER_EVENTS_BOX_ONLY;
    default: return RN_POINTER_EVENTS_AUTO;
  }
}

}  // namespace

GdkRGBA to_rgba(const SharedColor &color) {
  if (!color) return GdkRGBA{0, 0, 0, 0};
  ColorComponents c = colorComponentsFromColor(color);
  return GdkRGBA{c.red, c.green, c.blue, c.alpha};
}

void apply_view_props(GtkWidget *widget, const ViewProps &props,
                      const LayoutMetrics &layout) {
  gtk_widget_set_opacity(widget, props.opacity);
  rn_widget_set_pointer_events(widget, pointer_events(props.pointerEvents));
  gtk_widget_set_cursor_from_name(widget, cursor_name(props.cursor));
  if (RN_IS_VIEW(widget)) {
    // Keyboard focus (Tab, clicks, ref.focus()); see GtkKeyboardHandler.
    gtk_widget_set_focusable(widget, props.focusable);
    rn_view_set_focus_ring(RN_VIEW(widget), props.enableFocusRing);
  }

  // RN's matrix uses graphene's convention (row vectors, translation in
  // the last row). Like iOS's layer anchor point, it applies around the
  // view's centre; transform-origin is folded in relative to that.
  const auto &size = layout.frame.size;
  Transform t = props.resolveTransform(layout);
  if (t == Transform::Identity() || props.transform.operations.empty()) {
    rn_widget_set_transform(widget, nullptr);
  } else {
    float m[16];
    for (int i = 0; i < 16; i++) m[i] = t.matrix[i];
    graphene_matrix_t matrix, to_centre, from_centre, tmp;
    graphene_matrix_init_from_float(&matrix, m);
    graphene_point3d_t c, minus_c;
    graphene_point3d_init(&c, size.width / 2, size.height / 2, 0);
    graphene_point3d_init(&minus_c, -size.width / 2, -size.height / 2, 0);
    graphene_matrix_init_translate(&to_centre, &minus_c);
    graphene_matrix_init_translate(&from_centre, &c);
    graphene_matrix_multiply(&to_centre, &matrix, &tmp);
    graphene_matrix_multiply(&tmp, &from_centre, &matrix);
    rn_widget_set_transform(widget, &matrix);
  }

  if (RN_IS_VIEW(widget)) apply_view_style(widget, props, layout);
}

void apply_view_style(GtkWidget *widget, const ViewProps &props,
                      const LayoutMetrics &layout) {
  const auto &size = layout.frame.size;
  RNView *view = RN_VIEW(widget);
  RNViewStyle style = view_style(props, layout);
  rn_view_set_style(view, &style);
  auto shadows = box_shadows(props);
  rn_view_set_box_shadows(view, shadows.data(), shadows.size());
  auto f = filters(props);
  rn_view_set_filters(view, f.data(), f.size());
  apply_gradients(view, props, size.width, size.height);
}

}  // namespace rngtk
