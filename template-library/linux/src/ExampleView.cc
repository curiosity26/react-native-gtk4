#include "ExampleView.h"

#include <react/renderer/core/propsConversions.h>

#include <cstdio>

using namespace facebook::react;

namespace example {

extern const char ExampleViewComponentName[] = "RNGtkExampleView";

ExampleViewProps::ExampleViewProps(const PropsParserContext &context,
                                   const ExampleViewProps &sourceProps, const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      date(convertRawProp(context, rawProps, "date", sourceProps.date, {})),
      showWeekNumbers(
          convertRawProp(context, rawProps, "showWeekNumbers", sourceProps.showWeekNumbers, false)) {}

void ExampleViewEventEmitter::onDateChange(const std::string &date) const {
  dispatchEvent("dateChange", [date](facebook::jsi::Runtime &runtime) {
    auto payload = facebook::jsi::Object(runtime);
    payload.setProperty(runtime, "date", date);
    return payload;
  });
}

namespace {

// What the widget keeps: its view's event emitter, and whether the day is
// being set from props (no event for that).
struct Data {
  std::shared_ptr<const ExampleViewEventEmitter> emitter;
  bool fromProps = false;
};

Data *data_of(GtkWidget *widget) {
  return static_cast<Data *>(g_object_get_data(G_OBJECT(widget), "example-view"));
}

std::string iso_date(GDateTime *date) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", g_date_time_get_year(date),
                g_date_time_get_month(date), g_date_time_get_day_of_month(date));
  return buffer;
}

void select(GtkWidget *widget, GDateTime *date) {
  Data *data = data_of(widget);
  data->fromProps = true;
  gtk_calendar_select_day(GTK_CALENDAR(widget), date);
  data->fromProps = false;
}

}  // namespace

rngtk::NativeComponent exampleViewComponent() {
  rngtk::NativeComponent component;
  component.descriptor = concreteComponentDescriptorProvider<ExampleViewComponentDescriptor>();
  component.create = [](const ShadowView &) {
    GtkWidget *calendar = gtk_calendar_new();
    g_object_set_data_full(G_OBJECT(calendar), "example-view", new Data(),
                           [](gpointer p) { delete static_cast<Data *>(p); });
    g_signal_connect(calendar, "day-selected", G_CALLBACK(+[](GtkCalendar *c, gpointer) {
                       Data *data = data_of(GTK_WIDGET(c));
                       if (data->fromProps || !data->emitter) return;
                       GDateTime *date = gtk_calendar_get_date(c);
                       data->emitter->onDateChange(iso_date(date));
                       g_date_time_unref(date);
                     }),
                     nullptr);
    return calendar;
  };
  component.update = [](GtkWidget *widget, const ShadowView &oldView, const ShadowView &newView) {
    data_of(widget)->emitter =
        std::static_pointer_cast<const ExampleViewEventEmitter>(newView.eventEmitter);
    auto props = std::static_pointer_cast<const ExampleViewProps>(newView.props);
    auto old = std::static_pointer_cast<const ExampleViewProps>(oldView.props);
    gtk_calendar_set_show_week_numbers(GTK_CALENDAR(widget), props->showWeekNumbers);
    if (!old || old->date != props->date) {
      int year = 0, month = 0, day = 0;
      if (std::sscanf(props->date.c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
        GDateTime *date = g_date_time_new_local(year, month, day, 0, 0, 0);
        if (date) {
          select(widget, date);
          g_date_time_unref(date);
        }
      }
    }
  };
  component.command = [](GtkWidget *widget, const std::string &name, const folly::dynamic &) {
    if (name == "showToday") {
      GDateTime *now = g_date_time_new_now_local();
      gtk_calendar_select_day(GTK_CALENDAR(widget), now);  // reports it (onDateChange)
      g_date_time_unref(now);
    }
  };
  return component;
}

}  // namespace example
