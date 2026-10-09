// RNCNetInfo: the TurboModule @react-native-community/netinfo calls
// (src/internal/NativeRNCNetInfo.ts): getCurrentState(), and
// "netInfo.networkStatusDidChange" events while JS listens.
//
// GNetworkMonitor says whether there's a network, whether it reaches the
// internet (connectivity FULL) and whether it's metered (isConnectionExpensive).
// NetworkManager, when the system bus has it, says what kind it is (wifi,
// ethernet, cellular, vpn, bluetooth) and its address; without it (a
// Flatpak without --system-talk-name=org.freedesktop.NetworkManager) the
// type is "other".
#pragma once

#include <react/bridging/Promise.h>
#include <rngtk/CxxModule.h>

#include <memory>
#include <string>

namespace rngtk_netinfo {

struct Monitor;

class NetInfoModule : public rngtk::CxxModule<NetInfoModule> {
 public:
  static constexpr const char *kName = "RNCNetInfo";
  explicit NetInfoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~NetInfoModule() override;

  void configure(facebook::jsi::Runtime &rt, folly::dynamic config);
  facebook::react::AsyncPromise<folly::dynamic> getCurrentState(facebook::jsi::Runtime &rt,
                                                                std::optional<std::string> requestedInterface);
  void addListener(facebook::jsi::Runtime &rt, std::string eventName);
  void removeListeners(facebook::jsi::Runtime &rt, double count);

 private:
  std::shared_ptr<Monitor> monitor_;
};

// The state for netinfo's JS, from what GIO and NetworkManager report
// (exposed for tests): type is netinfo's NetInfoStateType.
folly::dynamic netInfoState(bool available, int connectivity, bool metered,
                            const std::string &type, const std::string &ssid,
                            const std::string &ipAddress, const std::string &subnet);

// A NetworkManager connection type ("802-11-wireless") as netinfo's.
std::string netInfoType(const std::string &nmType);

}  // namespace rngtk_netinfo
