import {AppRegistry} from 'react-native';
import App from './App';
import Gallery from './Gallery';
import GalleryAppearance from './GalleryAppearance';
import GalleryControls from './GalleryControls';
import GalleryImages from './GalleryImages';
import GalleryLists from './GalleryLists';
import GallerySelection from './GallerySelection';
import Showcase from './Showcase';

AppRegistry.registerComponent('HelloWorld', () => App);
AppRegistry.registerComponent('Gallery', () => Gallery);
AppRegistry.registerComponent('GalleryLists', () => GalleryLists);
AppRegistry.registerComponent('GalleryImages', () => GalleryImages);
AppRegistry.registerComponent('GalleryControls', () => GalleryControls);
AppRegistry.registerComponent('GalleryAppearance', () => GalleryAppearance);
AppRegistry.registerComponent('GallerySelection', () => GallerySelection);
AppRegistry.registerComponent('Showcase', () => Showcase);
