// A TurboModule whose methods are plain C++ member functions, bridged the
// way codegen's Cxx specs bridge theirs, for modules without a codegen spec
// (the host's Linux-only APIs, and libraries' modules: see
// rngtk/Extensions.h):
//
//   class Mine : public CxxModule<Mine> {
//    public:
//     Mine(std::shared_ptr<CallInvoker> js) : CxxModule("Mine", std::move(js)) {
//       method<&Mine::open>("open");
//     }
//     AsyncPromise<std::string> open(jsi::Runtime &rt, folly::dynamic options);
//   };
//
// Arguments convert with react/bridging (folly::dynamic, std::string,
// double, bool, std::optional, AsyncCallback, jsi types...); a missing one
// is undefined. Return values too (AsyncPromise is a Promise).
#pragma once

#include <ReactCommon/TurboModule.h>
#include <react/bridging/Bridging.h>

#include <memory>
#include <string>
#include <utility>

namespace rngtk {

template <typename T>
class CxxModule : public facebook::react::TurboModule {
 protected:
  CxxModule(std::string name, std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : TurboModule(std::move(name), std::move(jsInvoker)) {}

  template <auto Method>
  void method(const char *name) {
    methodMap_[name] = MethodMetadata{arity(Method), &invoke<Method>};
  }

 private:
  template <typename R, typename C, typename... Args>
  static constexpr size_t arity(R (C::*)(facebook::jsi::Runtime &, Args...)) {
    return sizeof...(Args);
  }

  template <auto Method>
  static facebook::jsi::Value invoke(facebook::jsi::Runtime &rt, TurboModule &module,
                                     const facebook::jsi::Value *args, size_t count) {
    return invokeWith<Method>(rt, module, args, count,
                              std::make_index_sequence<arity(Method)>{});
  }

  template <auto Method, size_t... I>
  static facebook::jsi::Value invokeWith(facebook::jsi::Runtime &rt, TurboModule &module,
                                         const facebook::jsi::Value *args, size_t count,
                                         std::index_sequence<I...>) {
    auto &self = static_cast<T &>(module);
    return facebook::react::bridging::callFromJs<facebook::jsi::Value>(
        rt, Method, self.jsInvoker_, &self,
        (I < count ? facebook::jsi::Value(rt, args[I]) : facebook::jsi::Value::undefined())...);
  }
};

}  // namespace rngtk
