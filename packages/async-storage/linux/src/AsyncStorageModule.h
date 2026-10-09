// RNAsyncStorage: the TurboModule @react-native-async-storage/async-storage
// 3.x calls (src/native-module/NativeAsyncStorage.ts). Each database is a
// Store; the legacy_* methods (AsyncStorage's default instance) use the
// "legacy" one. Work runs in order on one worker thread, off the JS thread.
#pragma once

#include <react/bridging/Promise.h>
#include <rngtk/CxxModule.h>

#include <string>

namespace rngtk_async_storage {

using Promise = facebook::react::AsyncPromise<folly::dynamic>;

class AsyncStorageModule : public rngtk::CxxModule<AsyncStorageModule> {
 public:
  static constexpr const char *kName = "RNAsyncStorage";
  explicit AsyncStorageModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

  Promise getValues(facebook::jsi::Runtime &rt, std::string db, folly::dynamic keys);
  Promise setValues(facebook::jsi::Runtime &rt, std::string db, folly::dynamic values);
  Promise removeValues(facebook::jsi::Runtime &rt, std::string db, folly::dynamic keys);
  Promise getKeys(facebook::jsi::Runtime &rt, std::string db);
  Promise clearStorage(facebook::jsi::Runtime &rt, std::string db);

  Promise legacy_multiGet(facebook::jsi::Runtime &rt, folly::dynamic keys);
  Promise legacy_multiSet(facebook::jsi::Runtime &rt, folly::dynamic pairs);
  Promise legacy_multiRemove(facebook::jsi::Runtime &rt, folly::dynamic keys);
  Promise legacy_multiMerge(facebook::jsi::Runtime &rt, folly::dynamic pairs);
  Promise legacy_getAllKeys(facebook::jsi::Runtime &rt);
  Promise legacy_clear(facebook::jsi::Runtime &rt);
};

}  // namespace rngtk_async_storage
