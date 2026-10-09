#include "AsyncStorageModule.h"

#include "Store.h"

#include <glib.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>

using namespace facebook::react;
namespace jsi = facebook::jsi;

namespace rngtk_async_storage {

namespace {

// The legacy AsyncStorage's database (the default instance's).
constexpr const char *kLegacy = "legacy";

// The open databases, for the process (they outlive reloads), and the one
// worker thread that touches them.
struct Work {
  std::function<folly::dynamic(Store &)> run;
  std::string db;
  Promise promise;
};

std::mutex storesMutex;
std::map<std::string, std::unique_ptr<Store>> stores;

Store &storeFor(const std::string &db) {
  std::lock_guard<std::mutex> lock(storesMutex);
  auto &store = stores[db];
  if (!store) store = std::make_unique<Store>(storePath(db));
  return *store;
}

void runWork(gpointer data, gpointer) {
  std::unique_ptr<Work> work(static_cast<Work *>(data));
  Store &store = storeFor(work->db);
  try {
    folly::dynamic result = work->run(store);
    store.flush();
    work->promise.resolve(std::move(result));
  } catch (const std::exception &e) {
    work->promise.reject(Error(std::string("AsyncStorage (") + store.path() + "): " + e.what()));
  }
}

GThreadPool *pool() {
  static GThreadPool *p = g_thread_pool_new(runWork, nullptr, 1, FALSE, nullptr);
  return p;
}

Promise queue(jsi::Runtime &rt, const std::shared_ptr<CallInvoker> &js, std::string db,
              std::function<folly::dynamic(Store &)> run) {
  Promise promise(rt, js);
  g_thread_pool_push(pool(), new Work{std::move(run), std::move(db), promise}, nullptr);
  return promise;
}

std::vector<std::string> strings(const folly::dynamic &list) {
  std::vector<std::string> out;
  if (!list.isArray()) return out;
  for (auto &v : list) {
    if (v.isString()) out.push_back(v.asString());
  }
  return out;
}

folly::dynamic keyList(Store &store) {
  folly::dynamic out = folly::dynamic::array;
  for (auto &k : store.keys()) out.push_back(k);
  return out;
}

}  // namespace

AsyncStorageModule::AsyncStorageModule(std::shared_ptr<CallInvoker> jsInvoker)
    : CxxModule(kName, std::move(jsInvoker)) {
  method<&AsyncStorageModule::getValues>("getValues");
  method<&AsyncStorageModule::setValues>("setValues");
  method<&AsyncStorageModule::removeValues>("removeValues");
  method<&AsyncStorageModule::getKeys>("getKeys");
  method<&AsyncStorageModule::clearStorage>("clearStorage");
  method<&AsyncStorageModule::legacy_multiGet>("legacy_multiGet");
  method<&AsyncStorageModule::legacy_multiSet>("legacy_multiSet");
  method<&AsyncStorageModule::legacy_multiRemove>("legacy_multiRemove");
  method<&AsyncStorageModule::legacy_multiMerge>("legacy_multiMerge");
  method<&AsyncStorageModule::legacy_getAllKeys>("legacy_getAllKeys");
  method<&AsyncStorageModule::legacy_clear>("legacy_clear");
}

// [{key, value | null}] for each key, in order.
Promise AsyncStorageModule::getValues(jsi::Runtime &rt, std::string db, folly::dynamic keys) {
  return queue(rt, jsInvoker_, db, [keys = strings(keys)](Store &store) {
    folly::dynamic out = folly::dynamic::array;
    for (auto &k : keys) {
      auto v = store.get(k);
      out.push_back(folly::dynamic::object("key", k)("value", v ? folly::dynamic(*v) : nullptr));
    }
    return out;
  });
}

// [{key, value}] in; a null value removes the key. Resolves with them.
Promise AsyncStorageModule::setValues(jsi::Runtime &rt, std::string db, folly::dynamic values) {
  return queue(rt, jsInvoker_, db, [values = std::move(values)](Store &store) {
    folly::dynamic out = folly::dynamic::array;
    if (!values.isArray()) return out;
    for (auto &entry : values) {
      auto *key = entry.get_ptr("key");
      auto *value = entry.get_ptr("value");
      if (!key || !key->isString()) continue;
      store.set(key->asString(), value && value->isString() ? std::optional(value->asString()) : std::nullopt);
      out.push_back(folly::dynamic::object("key", *key)("value", value ? *value : nullptr));
    }
    return out;
  });
}

Promise AsyncStorageModule::removeValues(jsi::Runtime &rt, std::string db, folly::dynamic keys) {
  return queue(rt, jsInvoker_, db, [keys = strings(keys)](Store &store) {
    for (auto &k : keys) store.remove(k);
    return folly::dynamic(nullptr);
  });
}

Promise AsyncStorageModule::getKeys(jsi::Runtime &rt, std::string db) {
  return queue(rt, jsInvoker_, db, keyList);
}

Promise AsyncStorageModule::clearStorage(jsi::Runtime &rt, std::string db) {
  return queue(rt, jsInvoker_, db, [](Store &store) {
    store.clear();
    return folly::dynamic(nullptr);
  });
}

// [[key, value | null]] for each key.
Promise AsyncStorageModule::legacy_multiGet(jsi::Runtime &rt, folly::dynamic keys) {
  return queue(rt, jsInvoker_, kLegacy, [keys = strings(keys)](Store &store) {
    folly::dynamic out = folly::dynamic::array;
    for (auto &k : keys) {
      auto v = store.get(k);
      out.push_back(folly::dynamic::array(k, v ? folly::dynamic(*v) : nullptr));
    }
    return out;
  });
}

Promise AsyncStorageModule::legacy_multiSet(jsi::Runtime &rt, folly::dynamic pairs) {
  return queue(rt, jsInvoker_, kLegacy, [pairs = std::move(pairs)](Store &store) {
    if (pairs.isArray()) {
      for (auto &p : pairs) {
        if (p.isArray() && p.size() == 2 && p[0].isString() && p[1].isString()) {
          store.set(p[0].asString(), p[1].asString());
        }
      }
    }
    return folly::dynamic(nullptr);
  });
}

Promise AsyncStorageModule::legacy_multiRemove(jsi::Runtime &rt, folly::dynamic keys) {
  return queue(rt, jsInvoker_, kLegacy, [keys = strings(keys)](Store &store) {
    for (auto &k : keys) store.remove(k);
    return folly::dynamic(nullptr);
  });
}

Promise AsyncStorageModule::legacy_multiMerge(jsi::Runtime &rt, folly::dynamic pairs) {
  return queue(rt, jsInvoker_, kLegacy, [pairs = std::move(pairs)](Store &store) {
    if (pairs.isArray()) {
      for (auto &p : pairs) {
        if (p.isArray() && p.size() == 2 && p[0].isString() && p[1].isString()) {
          store.merge(p[0].asString(), p[1].asString());
        }
      }
    }
    return folly::dynamic(nullptr);
  });
}

Promise AsyncStorageModule::legacy_getAllKeys(jsi::Runtime &rt) {
  return queue(rt, jsInvoker_, kLegacy, keyList);
}

Promise AsyncStorageModule::legacy_clear(jsi::Runtime &rt) {
  return queue(rt, jsInvoker_, kLegacy, [](Store &store) {
    store.clear();
    return folly::dynamic(nullptr);
  });
}

}  // namespace rngtk_async_storage
