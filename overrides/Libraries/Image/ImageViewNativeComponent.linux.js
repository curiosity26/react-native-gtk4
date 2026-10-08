/**
 * Linux override of react-native/Libraries/Image/ImageViewNativeComponent
 * (RN 0.87.1, imports rewritten to react-native-upstream/...).
 *
 * The only change: Linux uses Android's RCTImageView view config. Image on
 * Linux is Image.android.js (see Image.linux.js), which sends Android's
 * props: `defaultSource` as a URI string, which iOS's config drops, and
 * `shouldNotifyLoadEvents`/`headers`, which it doesn't list.
 */
/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @flow strict-local
 * @format
 */

import type {HostComponent} from 'react-native-upstream/src/private/types/HostComponent';
import type {HostInstance} from 'react-native-upstream/src/private/types/HostInstance';
import type {ViewProps} from 'react-native-upstream/Libraries/Components/View/ViewPropTypes';
import type {PartialViewConfig} from 'react-native-upstream/Libraries/Renderer/shims/ReactNativeTypes';
import type {
  ColorValue,
  DangerouslyImpreciseStyle,
  ImageStyleProp,
} from 'react-native-upstream/Libraries/StyleSheet/StyleSheet';
import type {ResolvedAssetSource} from 'react-native-upstream/Libraries/Image/AssetSourceResolver';
import type {ImageProps} from 'react-native-upstream/Libraries/Image/ImageProps';
import type {ImageSource} from 'react-native-upstream/Libraries/Image/ImageSource';

import {colorAttribute} from 'react-native-upstream/Libraries/Components/View/ReactNativeStyleAttributes';
import * as NativeComponentRegistry from 'react-native-upstream/Libraries/NativeComponent/NativeComponentRegistry';
import {ConditionallyIgnoredEventHandlers} from 'react-native-upstream/Libraries/NativeComponent/ViewConfigIgnore';
import codegenNativeCommands from 'react-native-upstream/Libraries/Utilities/codegenNativeCommands';
import Platform from 'react-native-upstream/Libraries/Utilities/Platform';

type ImageHostComponentProps = Readonly<{
  ...ImageProps,
  ...ViewProps,

  style?: ImageStyleProp | DangerouslyImpreciseStyle,

  // iOS native props
  tintColor?: ColorValue,

  // Android native props
  shouldNotifyLoadEvents?: boolean,
  src?: ?ResolvedAssetSource | ?ReadonlyArray<?Readonly<{uri?: ?string, ...}>>,
  headers?: ?{[string]: string},
  defaultSource?: ?ImageSource | ?string,
  loadingIndicatorSrc?: ?string,
}>;

interface NativeCommands {
  readonly setIsVisible_EXPERIMENTAL: (
    viewRef: HostInstance,
    isVisible: boolean,
    time: number,
  ) => void;
}

export const Commands: NativeCommands = codegenNativeCommands<NativeCommands>({
  supportedCommands: ['setIsVisible_EXPERIMENTAL'],
});

export const __INTERNAL_VIEW_CONFIG: PartialViewConfig =
  Platform.OS === 'android' || Platform.OS === 'linux'
    ? {
        uiViewClassName: 'RCTImageView',
        bubblingEventTypes: {},
        directEventTypes: {
          topLoadStart: {
            registrationName: 'onLoadStart',
          },
          topProgress: {
            registrationName: 'onProgress',
          },
          topError: {
            registrationName: 'onError',
          },
          topLoad: {
            registrationName: 'onLoad',
          },
          topLoadEnd: {
            registrationName: 'onLoadEnd',
          },
        },
        validAttributes: {
          blurRadius: true,
          defaultSource: true,
          internal_analyticTag: true,
          resizeMethod: true,
          resizeMode: true,
          resizeMultiplier: true,
          tintColor: colorAttribute,
          borderBottomLeftRadius: true,
          borderTopLeftRadius: true,
          src: true,
          // NOTE: New Architecture expects this to be called `source`,
          // regardless of the platform, therefore propagate it as well.
          // For the backwards compatibility reasons, we keep both `src`
          // and `source`, which will be identical at this stage.
          source: true,
          borderRadius: true,
          headers: true,
          shouldNotifyLoadEvents: true,
          overlayColor: colorAttribute,
          borderColor: colorAttribute,
          accessible: true,
          progressiveRenderingEnabled: true,
          fadeDuration: true,
          borderBottomRightRadius: true,
          borderTopRightRadius: true,
          loadingIndicatorSrc: true,
        },
      }
    : {
        uiViewClassName: 'RCTImageView',
        bubblingEventTypes: {},
        directEventTypes: {
          topLoadStart: {
            registrationName: 'onLoadStart',
          },
          topProgress: {
            registrationName: 'onProgress',
          },
          topError: {
            registrationName: 'onError',
          },
          topPartialLoad: {
            registrationName: 'onPartialLoad',
          },
          topLoad: {
            registrationName: 'onLoad',
          },
          topLoadEnd: {
            registrationName: 'onLoadEnd',
          },
        },
        validAttributes: {
          blurRadius: true,
          capInsets: {
            diff: require('react-native-upstream/Libraries/Utilities/differ/insetsDiffer').default,
          },
          defaultSource: {
            process: require('react-native-upstream/Libraries/Image/resolveAssetSource').default,
          },
          internal_analyticTag: true,
          resizeMode: true,
          source: true,
          tintColor: colorAttribute,
          ...ConditionallyIgnoredEventHandlers({
            onLoadStart: true,
            onLoad: true,
            onLoadEnd: true,
            onProgress: true,
            onError: true,
            onPartialLoad: true,
          }),
        },
      };

const ImageViewNativeComponent: HostComponent<ImageHostComponentProps> =
  NativeComponentRegistry.get<ImageHostComponentProps>(
    'RCTImageView',
    () => __INTERNAL_VIEW_CONFIG,
  );

export default ImageViewNativeComponent;
