// The JS side of the Example TurboModule (linux/src/ExampleModule.cc).
import {TurboModuleRegistry, type TurboModule} from 'react-native';

export interface Spec extends TurboModule {
  greet(name: string): string;
  gtkVersion(): Promise<string>;
  describe(value: Object): {type: string; size: number};
}

// null off Linux, or if the app wasn't built with this library.
export default TurboModuleRegistry.get<Spec>('Example');
