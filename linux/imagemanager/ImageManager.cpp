// The GTK platform's ImageManager, replacing ReactCommon's unimplemented
// cxx one (see linux/CMakeLists.txt). <Image>'s shadow node calls
// requestImage(); GtkImageLoader (found in the context container) loads the
// source and completes the request's observer coordinator, which
// GtkMountingManager observes to draw the texture and send onLoad/onError.
#include <react/renderer/imagemanager/ImageManager.h>

#include "GtkImageLoader.h"

#include <react/renderer/imagemanager/ImageResponse.h>
#include <react/renderer/imagemanager/ImageResponseObserverCoordinator.h>
#include <react/utils/ContextContainer.h>

namespace facebook::react {

// Outside Android the header only has `void *self_` for platform state:
// it holds the context container.
using SharedContextContainer = std::shared_ptr<const ContextContainer>;

ImageManager::ImageManager(
    const std::shared_ptr<const ContextContainer>& contextContainer)
    : self_(new SharedContextContainer(contextContainer)) {}

ImageManager::~ImageManager() {
  delete static_cast<SharedContextContainer*>(self_);
}

ImageRequest ImageManager::requestImage(
    const ImageSource& imageSource,
    SurfaceId /*surfaceId*/,
    const ImageRequestParams& /*imageRequestParams*/,
    Tag /*tag*/) const {
  ImageRequest request(imageSource, nullptr);
  const auto& contextContainer = *static_cast<SharedContextContainer*>(self_);
  if (imageSource.uri.empty() || !contextContainer) return request;
  auto loader =
      contextContainer->find<std::shared_ptr<rngtk::GtkImageLoader>>(
          rngtk::GtkImageLoader::kContextKey);
  if (!loader.has_value()) return request;

  std::weak_ptr<const ImageResponseObserverCoordinator> weak =
      request.getSharedObserverCoordinator();
  (*loader)->load(
      imageSource.uri, imageSource.headers,
      [weak](const rngtk::GtkImageLoader::Result& result) {
        auto coordinator = weak.lock();
        if (!coordinator) return;  // the image went away
        if (result.texture) {
          coordinator->nativeImageResponseComplete(ImageResponse(
              std::static_pointer_cast<void>(result.texture), nullptr));
        } else {
          auto info = std::make_shared<ImageErrorInfo>();
          info->error = result.error;
          info->responseCode = result.status;
          coordinator->nativeImageResponseFailed(
              ImageLoadError(std::static_pointer_cast<void>(info)));
        }
      });
  return request;
}

} // namespace facebook::react
