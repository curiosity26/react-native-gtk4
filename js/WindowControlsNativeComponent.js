/**
 * The JS side of RNGtkWindowControls
 * (linux/components/GtkWindowControlsShadowNode.h): GtkWindowControls.
 * React Native's Babel codegen plugin turns this into its view config.
 *
 * @flow strict-local
 */
import type {ViewProps} from 'react-native/Libraries/Components/View/ViewPropTypes';
import type {HostComponent} from 'react-native';

import {codegenNativeComponent} from 'react-native';

type NativeProps = $ReadOnly<{
  ...ViewProps,
  // 'start' or 'end' of the title bar.
  side?: string,
}>;

export default (codegenNativeComponent<NativeProps>(
  'RNGtkWindowControls',
): HostComponent<NativeProps>);
