// Example: a native module and a native component for React Native on
// Linux. `Example` is null where the native side isn't there.
import ExampleView, {Commands} from './ExampleViewNativeComponent';

export {default as Example} from './NativeExample';
// (Re-exported under another name: codegen keeps `Commands` to the file
// that makes them.)
export const ExampleViewCommands = Commands;
export {ExampleView};
