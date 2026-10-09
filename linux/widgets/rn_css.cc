#include "rn_css.h"

#include <string>

namespace {

struct ScopedCss {
  GtkCssProvider *provider;
  GdkDisplay *display;
  std::string klass;
  ~ScopedCss() {
    gtk_style_context_remove_provider_for_display(display,
                                                  GTK_STYLE_PROVIDER(provider));
    g_object_unref(provider);
  }
};

GQuark css_quark() {
  static GQuark q = g_quark_from_static_string("rn-scoped-css");
  return q;
}

}  // namespace

void rn_widget_set_css(GtkWidget *widget, const char *css) {
  // Per-widget style-context providers don't reach child widgets, so this
  // uses a display provider and a class unique to the widget.
  auto *scoped = static_cast<ScopedCss *>(
      g_object_get_qdata(G_OBJECT(widget), css_quark()));
  if (!scoped) {
    static unsigned next = 0;
    scoped = new ScopedCss{gtk_css_provider_new(), gtk_widget_get_display(widget),
                           "rn-css-" + std::to_string(next++)};
    gtk_widget_add_css_class(widget, scoped->klass.c_str());
    gtk_style_context_add_provider_for_display(
        scoped->display, GTK_STYLE_PROVIDER(scoped->provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_set_qdata_full(G_OBJECT(widget), css_quark(), scoped,
                            [](gpointer p) { delete static_cast<ScopedCss *>(p); });
  }
  std::string text = css ? css : "";
  std::string self = "." + scoped->klass;
  for (size_t at = text.find('&'); at != std::string::npos;
       at = text.find('&', at + self.size())) {
    text.replace(at, 1, self);
  }
  gtk_css_provider_load_from_string(scoped->provider, text.c_str());
}

GdkRGBA rn_theme_accent(GtkWidget *widget) {
  GdkRGBA accent{0.21f, 0.52f, 0.89f, 1};  // #3584E4
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  GtkStyleContext *style = gtk_widget_get_style_context(widget);
  if (!gtk_style_context_lookup_color(style, "accent_bg_color", &accent)) {
    gtk_style_context_lookup_color(style, "theme_selected_bg_color", &accent);
  }
  G_GNUC_END_IGNORE_DEPRECATIONS
  return accent;
}
