// <Image> support in GtkMountingManager: observes each image's request
// (made by ImageManager from the shadow node), draws the texture in the
// RNView with the image props, and sends the load events.
#include "GtkImageLoader.h"
#include "GtkMountingManager.h"
#include "GtkViewProps.h"

#include "rn_view.h"

#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageShadowNode.h>

using namespace facebook::react;

namespace rngtk {

namespace {

RNImageFit image_fit(ImageResizeMode mode) {
  switch (mode) {
    case ImageResizeMode::Cover: return RN_IMAGE_COVER;
    case ImageResizeMode::Contain: return RN_IMAGE_CONTAIN;
    case ImageResizeMode::Stretch: return RN_IMAGE_STRETCH;
    case ImageResizeMode::Center: return RN_IMAGE_CENTER;
    case ImageResizeMode::Repeat: return RN_IMAGE_REPEAT;
    case ImageResizeMode::None: return RN_IMAGE_NONE;
  }
  return RN_IMAGE_COVER;
}

}  // namespace

// The coordinator calls back on the thread that completes the request,
// which is the main thread (GtkImageLoader delivers there), or right away
// in addObserver() if the image is already loaded.
class GtkMountingManager::ImageObserver : public ImageResponseObserver {
 public:
  ImageObserver(std::weak_ptr<GtkMountingManager> manager, Tag tag)
      : manager_(std::move(manager)), tag_(tag) {}

  void didReceiveProgress(float, int64_t, int64_t) const override {}

  void didReceiveImage(const ImageResponse &response) const override {
    auto texture = std::static_pointer_cast<GdkTexture>(response.getImage());
    if (auto manager = manager_.lock(); manager && texture) {
      manager->imageLoaded(tag_, texture.get());
    }
  }

  void didReceiveFailure(const ImageLoadError &error) const override {
    auto info = std::static_pointer_cast<ImageErrorInfo>(error.getError());
    if (auto manager = manager_.lock()) {
      manager->imageFailed(tag_, info ? *info : ImageErrorInfo{"Unknown", 0, {}});
    }
  }

 private:
  std::weak_ptr<GtkMountingManager> manager_;
  Tag tag_;
};

void GtkMountingManager::updateImage(GtkWidget *widget,
                                     const ShadowView &oldView,
                                     const ShadowView &newView) {
  auto &tracking = images_[newView.tag];
  auto props = std::dynamic_pointer_cast<const ImageProps>(newView.props);

  // Style changes redraw the image we have.
  if (oldView.props != newView.props && tracking.loaded) {
    applyImage(newView.tag, rn_view_get_image(RN_VIEW(widget)));
  }

  auto state =
      std::dynamic_pointer_cast<const ImageShadowNode::ConcreteState>(
          newView.state);
  if (!state) return;
  auto coordinator =
      state->getData().getImageRequest().getSharedObserverCoordinator();
  if (coordinator == tracking.coordinator) return;

  // A new request (new source): swap observers.
  if (tracking.coordinator && tracking.observer) {
    tracking.coordinator->removeObserver(tracking.observer);
  }
  tracking.coordinator = coordinator;
  tracking.loaded = false;
  tracking.observer = std::make_shared<ImageObserver>(weak_from_this(),
                                                      newView.tag);
  if (state->getData().getImageSource().uri.empty()) return;

  // Always, like iOS: on Linux Image uses iOS's view config, which doesn't
  // send shouldNotifyLoadEvents.
  {
    if (auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(
            newView.eventEmitter)) {
      emitter->onLoadStart();
    }
  }
  // Shown until the image arrives.
  if (props && !props->defaultSource.uri.empty() && imageLoader_ &&
      !rn_view_get_image(RN_VIEW(widget))) {
    Tag tag = newView.tag;
    imageLoader_->load(
        props->defaultSource.uri, {},
        [weak = weak_from_this(), tag](const GtkImageLoader::Result &result) {
          auto self = weak.lock();
          if (!self || !result.texture) return;
          auto it = self->images_.find(tag);
          if (it != self->images_.end() && !it->second.loaded) {
            self->applyImage(tag, result.texture.get());
          }
        });
  }
  if (coordinator) coordinator->addObserver(tracking.observer);
}

void GtkMountingManager::applyImage(Tag tag, GdkTexture *texture) {
  GtkWidget *widget = viewForTag(tag);
  auto view = shadowViews_.find(tag);
  if (!widget || !RN_IS_VIEW(widget) || view == shadowViews_.end()) return;
  auto props = std::dynamic_pointer_cast<const ImageProps>(view->second.props);
  if (!props) return;
  float scale = 1;
  if (auto state = std::dynamic_pointer_cast<const ImageShadowNode::ConcreteState>(
          view->second.state)) {
    float s = state->getData().getImageSource().scale;
    if (s > 0) scale = s;
  }
  GdkRGBA tint = to_rgba(props->tintColor);
  rn_view_set_image(RN_VIEW(widget), texture, scale,
                    image_fit(props->resizeMode),
                    props->tintColor ? &tint : nullptr, props->blurRadius);
}

void GtkMountingManager::imageLoaded(Tag tag, GdkTexture *texture) {
  auto it = images_.find(tag);
  if (it == images_.end()) return;
  it->second.loaded = true;
  applyImage(tag, texture);
  auto view = shadowViews_.find(tag);
  if (view == shadowViews_.end()) return;
  auto props = std::dynamic_pointer_cast<const ImageProps>(view->second.props);
  auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(
      view->second.eventEmitter);
  auto state = std::dynamic_pointer_cast<const ImageShadowNode::ConcreteState>(
      view->second.state);
  if (!props || !emitter || !state) return;
  // onLoad reports the source with the image's size.
  ImageSource source = state->getData().getImageSource();
  source.size = {.width = Float(gdk_texture_get_width(texture)),
                 .height = Float(gdk_texture_get_height(texture))};
  emitter->onLoad(source);
  emitter->onLoadEnd();
}

void GtkMountingManager::imageFailed(Tag tag, const ImageErrorInfo &error) {
  auto view = shadowViews_.find(tag);
  if (view == shadowViews_.end()) return;
  auto props = std::dynamic_pointer_cast<const ImageProps>(view->second.props);
  auto emitter = std::dynamic_pointer_cast<const ImageEventEmitter>(
      view->second.eventEmitter);
  if (!props || !emitter) return;
  emitter->onError(error);
  emitter->onLoadEnd();
}

void GtkMountingManager::forgetImage(Tag tag) {
  auto it = images_.find(tag);
  if (it == images_.end()) return;
  if (it->second.coordinator && it->second.observer) {
    it->second.coordinator->removeObserver(it->second.observer);
  }
  images_.erase(it);
}

}  // namespace rngtk
