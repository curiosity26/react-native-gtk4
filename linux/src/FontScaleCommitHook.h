// Text re-measured when the font scale changes, on React Native versions
// that don't do it reliably themselves (0.83).
//
// There, SurfaceHandler dirties the measurable nodes (Text, TextInput) of
// the tree it commits when the font scale changes, but later commits can
// bring back nodes cloned from ones laid out at the old scale (React keeps
// its own nodes and shares them between revisions), and Yoga reuses their
// layout: Text keeps the old scale's size. React Native fixed this upstream
// (facebook/react-native#57246: each node's layout records its font scale,
// LayoutMetrics::fontSizeMultiplier, and a stale one is dirtied on every
// commit). This hook does the same from outside: on each commit after the
// font scale first changes, a measurable node that is laid out (clean) but
// wasn't committed at the current scale is cloned and dirtied, and the path
// to it cloned, so this commit's layout measures it again.
#pragma once

#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/uimanager/UIManagerCommitHook.h>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace rngtk {

class FontScaleCommitHook final : public facebook::react::UIManagerCommitHook {
 public:
  // Whether React Native needs it: false when layouts record their font
  // scale (the upstream fix).
  static constexpr bool kNeeded =
      []<class Metrics>(Metrics *) { return !requires(Metrics m) { m.fontSizeMultiplier; }; }(
          static_cast<facebook::react::LayoutMetrics *>(nullptr));

  void commitHookWasRegistered(const facebook::react::UIManager &) noexcept override {}
  void commitHookWasUnregistered(const facebook::react::UIManager &) noexcept override {}
  facebook::react::RootShadowNode::Unshared shadowTreeWillCommit(
      const facebook::react::ShadowTree &shadowTree,
      const facebook::react::RootShadowNode::Shared &oldRootShadowNode,
      const facebook::react::RootShadowNode::Unshared &newRootShadowNode,
      const facebook::react::ShadowTreeCommitOptions &commitOptions) noexcept override;

 private:
  std::shared_ptr<const facebook::react::ShadowNode> refresh(
      const std::shared_ptr<const facebook::react::ShadowNode> &node);
  bool isCurrent(const facebook::react::ShadowNode &node) const;

  std::mutex mutex_;
  // Per surface: the font scale of its first commit (nothing to do until it
  // changes), and the one being committed now.
  std::unordered_map<facebook::react::SurfaceId, float> initialScale_;
  std::optional<float> scale_;
  // Measurable nodes committed (so laid out) at scale_.
  std::unordered_map<const facebook::react::ShadowNode *,
                     std::weak_ptr<const facebook::react::ShadowNode>>
      current_;
};

}  // namespace rngtk
