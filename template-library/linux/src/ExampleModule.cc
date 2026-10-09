#include "ExampleModule.h"

#include <gtk/gtk.h>

using namespace facebook::react;

namespace example {

ExampleModule::ExampleModule(std::shared_ptr<CallInvoker> jsInvoker)
    : CxxModule(kName, std::move(jsInvoker)) {
  method<&ExampleModule::greet>("greet");
  method<&ExampleModule::gtkVersion>("gtkVersion");
  method<&ExampleModule::describe>("describe");
}

std::string ExampleModule::greet(facebook::jsi::Runtime &, std::string name) {
  return "Hello, " + name + ", from C++!";
}

AsyncPromise<std::string> ExampleModule::gtkVersion(facebook::jsi::Runtime &rt) {
  AsyncPromise<std::string> promise(rt, jsInvoker_);
  // GTK only on the main thread: hop there, resolve from there.
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *p = static_cast<AsyncPromise<std::string> *>(data);
        p->resolve(std::to_string(gtk_get_major_version()) + "." +
                   std::to_string(gtk_get_minor_version()) + "." +
                   std::to_string(gtk_get_micro_version()));
        return G_SOURCE_REMOVE;
      },
      new AsyncPromise<std::string>(promise),
      [](gpointer data) { delete static_cast<AsyncPromise<std::string> *>(data); });
  return promise;
}

folly::dynamic ExampleModule::describe(facebook::jsi::Runtime &, folly::dynamic value) {
  return folly::dynamic::object("type", value.typeName())("size",
                                                          value.isObject() || value.isArray()
                                                              ? int64_t(value.size())
                                                              : int64_t(0));
}

}  // namespace example
