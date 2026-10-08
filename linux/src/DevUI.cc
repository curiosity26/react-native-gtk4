#include "DevUI.h"

#include <glog/logging.h>
#include <react/renderer/graphics/Color.h>

#include <cstdio>

using namespace facebook::react;

namespace rngtk {

namespace {

constexpr const char *kBlue = "#2584E8";
constexpr const char *kRed = "#D32F2F";
constexpr const char *kWhite = "#FFFFFF";

std::string cssColor(const SharedColor &color, const char *fallback) {
  if (!color) return fallback;
  ColorComponents c = colorComponentsFromColor(color);
  char buf[64];
  snprintf(buf, sizeof buf, "rgba(%d,%d,%d,%.3f)", int(c.red * 255),
           int(c.green * 255), int(c.blue * 255), c.alpha);
  return buf;
}

}  // namespace

std::shared_ptr<DevUI> DevUI::create(GtkOverlay *overlay, GMenuModel *menu) {
  std::shared_ptr<DevUI> ui(new DevUI());

  ui->banner_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(ui->banner_, "rngtk-dev-banner");
  gtk_widget_set_valign(ui->banner_, GTK_ALIGN_START);
  gtk_widget_set_halign(ui->banner_, GTK_ALIGN_FILL);
  gtk_widget_set_can_target(ui->banner_, TRUE);
  ui->bannerLabel_ = gtk_label_new("");
  gtk_widget_set_hexpand(ui->bannerLabel_, TRUE);
  gtk_label_set_ellipsize(GTK_LABEL(ui->bannerLabel_), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(ui->banner_), ui->bannerLabel_);
  ui->resumeButton_ = gtk_button_new_with_label("Resume");
  gtk_box_append(GTK_BOX(ui->banner_), ui->resumeButton_);
  g_signal_connect(ui->resumeButton_, "clicked",
                   G_CALLBACK(+[](GtkButton *, gpointer data) {
                     auto *self = static_cast<DevUI *>(data);
                     if (self->resumeDebugger_) self->resumeDebugger_();
                   }),
                   ui.get());
  gtk_widget_set_visible(ui->banner_, FALSE);
  gtk_overlay_add_overlay(overlay, ui->banner_);

  ui->menuButton_ = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(ui->menuButton_),
                                "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(ui->menuButton_), menu);
  gtk_widget_set_tooltip_text(ui->menuButton_, "Dev menu (Ctrl+D)");
  gtk_widget_add_css_class(ui->menuButton_, "rngtk-dev-menu");
  gtk_widget_add_css_class(ui->menuButton_, "osd");
  gtk_widget_set_halign(ui->menuButton_, GTK_ALIGN_END);
  gtk_widget_set_valign(ui->menuButton_, GTK_ALIGN_END);
  gtk_widget_set_margin_end(ui->menuButton_, 8);
  gtk_widget_set_margin_bottom(ui->menuButton_, 8);
  gtk_overlay_add_overlay(overlay, ui->menuButton_);

  ui->css_ = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(GTK_WIDGET(overlay)),
      GTK_STYLE_PROVIDER(ui->css_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  return ui;
}

DevUI::~DevUI() noexcept {
  if (css_) {
    gtk_style_context_remove_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(css_));
    g_object_unref(css_);
  }
}

void DevUI::onMain(std::function<void(DevUI &)> fn) {
  struct Call {
    std::weak_ptr<DevUI> ui;
    std::function<void(DevUI &)> fn;
  };
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *call = static_cast<Call *>(data);
        if (auto ui = call->ui.lock()) call->fn(*ui);
        return G_SOURCE_REMOVE;
      },
      new Call{weak_from_this(), std::move(fn)},
      [](gpointer data) { delete static_cast<Call *>(data); });
}

void DevUI::setBanner(const std::string &message, const std::string &fg,
                      const std::string &bg) {
  LOG(INFO) << "dev banner: " << message;
  std::string css = ".rngtk-dev-banner { padding: 6px 12px; background: " +
                    bg + "; color: " + fg + "; font-size: 13px; }";
  gtk_css_provider_load_from_string(css_, css.c_str());
  gtk_label_set_text(GTK_LABEL(bannerLabel_), message.c_str());
  gtk_widget_set_visible(resumeButton_, resumeDebugger_ != nullptr);
  gtk_widget_set_visible(banner_, TRUE);
}

void DevUI::hideBanner() {
  gtk_widget_set_visible(banner_, FALSE);
  gtk_label_set_text(GTK_LABEL(bannerLabel_), "");
}

void DevUI::showDownloadBundleProgress() {
  onMain([](DevUI &ui) {
    ui.setBanner("Loading from Metro…", kWhite, kBlue);
  });
}

void DevUI::hideDownloadBundleProgress() {
  onMain([](DevUI &ui) { ui.hideBanner(); });
}

void DevUI::showLoadingView(const std::string &message, SharedColor textColor,
                            SharedColor backgroundColor) {
  std::string fg = cssColor(textColor, kWhite);
  std::string bg = cssColor(backgroundColor, kBlue);
  onMain([message, fg, bg](DevUI &ui) { ui.setBanner(message, fg, bg); });
}

void DevUI::hideLoadingView() {
  onMain([](DevUI &ui) { ui.hideBanner(); });
}

void DevUI::showDebuggerOverlay(std::function<void()> &&resumeDebuggerFn) {
  onMain([resume = std::move(resumeDebuggerFn)](DevUI &ui) mutable {
    ui.resumeDebugger_ = std::move(resume);
    ui.setBanner("Paused in debugger", "#1C1C1E", "#FFD60A");
  });
}

void DevUI::hideDebuggerOverlay() {
  onMain([](DevUI &ui) {
    ui.resumeDebugger_ = nullptr;
    ui.hideBanner();
  });
}

void DevUI::showError(const std::string &message) {
  setBanner(message, kWhite, kRed);
}

void DevUI::popupMenu() {
  gtk_menu_button_popup(GTK_MENU_BUTTON(menuButton_));
}

std::string DevUI::bannerText() const {
  if (!gtk_widget_get_visible(banner_)) return "";
  return gtk_label_get_text(GTK_LABEL(bannerLabel_));
}

}  // namespace rngtk
