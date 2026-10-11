#include "FontScaleCommitHook.h"

#include <react/renderer/components/view/YogaLayoutableShadowNode.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/mounting/ShadowTree.h>

#include <vector>

using namespace facebook::react;

namespace rngtk {

RootShadowNode::Unshared FontScaleCommitHook::shadowTreeWillCommit(
    const ShadowTree &shadowTree, const RootShadowNode::Shared & /*oldRootShadowNode*/,
    const RootShadowNode::Unshared &newRootShadowNode,
    const ShadowTreeCommitOptions & /*commitOptions*/) noexcept {
  float scale = newRootShadowNode->getConcreteProps().layoutContext.fontSizeMultiplier;
  std::lock_guard<std::mutex> lock(mutex_);
  float initial = initialScale_.try_emplace(shadowTree.getSurfaceId(), scale).first->second;
  if (!scale_ && scale == initial) return newRootShadowNode;
  if (scale_ != scale) {
    scale_ = scale;
    current_.clear();
  }

  auto children = std::make_shared<std::vector<std::shared_ptr<const ShadowNode>>>(
      newRootShadowNode->getChildren());
  bool changed = false;
  for (auto &child : *children) {
    if (!std::dynamic_pointer_cast<const YogaLayoutableShadowNode>(child)) continue;
    auto refreshed = refresh(child);
    if (refreshed != child) {
      child = std::move(refreshed);
      changed = true;
    }
  }
  // Forget nodes that are gone.
  if (current_.size() > 1024) {
    std::erase_if(current_, [](const auto &entry) { return entry.second.expired(); });
  }
  if (!changed) return newRootShadowNode;
  return std::static_pointer_cast<RootShadowNode>(
      newRootShadowNode->ShadowNode::clone({.children = children}));
}

bool FontScaleCommitHook::isCurrent(const ShadowNode &node) const {
  auto it = current_.find(&node);
  return it != current_.end() && it->second.lock().get() == &node;
}

// `node` with its stale measurable nodes (laid out, but not at scale_)
// dirtied, cloning the path to them; itself if there are none.
std::shared_ptr<const ShadowNode> FontScaleCommitHook::refresh(
    const std::shared_ptr<const ShadowNode> &node) {
  std::shared_ptr<std::vector<std::shared_ptr<const ShadowNode>>> children;
  const auto &oldChildren = node->getChildren();
  for (size_t i = 0; i < oldChildren.size(); i++) {
    if (!std::dynamic_pointer_cast<const YogaLayoutableShadowNode>(oldChildren[i])) continue;
    auto refreshed = refresh(oldChildren[i]);
    if (refreshed == oldChildren[i]) continue;
    if (!children) {
      children =
          std::make_shared<std::vector<std::shared_ptr<const ShadowNode>>>(oldChildren);
    }
    (*children)[i] = std::move(refreshed);
  }

  bool measurable = node->getTraits().check(ShadowNodeTraits::Trait::MeasurableYogaNode);
  bool stale = measurable &&
               static_cast<const YogaLayoutableShadowNode &>(*node).getIsLayoutClean() &&
               !isCurrent(*node);
  if (!children && !stale) {
    if (measurable) current_[node.get()] = node;
    return node;
  }

  auto clone = node->getComponentDescriptor().cloneShadowNode(
      *node, {.children = children ? children : ShadowNodeFragment::childrenPlaceholder(),
              .state = node->getState()});
  if (measurable) {
    std::static_pointer_cast<YogaLayoutableShadowNode>(clone)->dirtyLayout();
    current_[clone.get()] = clone;
  }
  return clone;
}

}  // namespace rngtk
