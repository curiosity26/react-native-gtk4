// RNGtkAnimatedProbe: a harness-only library component (rn-gtk-host's
// main.cc) whose `value` prop GalleryNativeModule animates on the native
// driver. Its update() must see the values frame by frame, as
// react-native-svg's elements do (the regression check for library
// components and Native Animated).
import {codegenNativeComponent, type CodegenTypes, type ViewProps} from 'react-native';

export interface NativeProps extends ViewProps {
  value?: CodegenTypes.Float;
}

export default codegenNativeComponent<NativeProps>('RNGtkAnimatedProbe');
