// The JS side of RNGtkExampleView (linux/src/ExampleView.cc). React
// Native's Babel codegen plugin turns this into the component's view
// config.
import type * as React from 'react';
import {
  codegenNativeCommands,
  codegenNativeComponent,
  type CodegenTypes,
  type HostComponent,
  type ViewProps,
} from 'react-native';

export interface NativeProps extends ViewProps {
  // 'YYYY-MM-DD': the selected day.
  date?: string;
  showWeekNumbers?: boolean;
  onDateChange?: CodegenTypes.DirectEventHandler<Readonly<{date: string}>>;
}

export interface NativeCommands {
  showToday: (viewRef: React.ElementRef<HostComponent<NativeProps>>) => void;
}

export const Commands = codegenNativeCommands<NativeCommands>({
  supportedCommands: ['showToday'],
});

export default codegenNativeComponent<NativeProps>('RNGtkExampleView');
