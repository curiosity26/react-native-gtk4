import {AppRegistry} from 'react-native';
import App from './App';
import Gallery from './Gallery';
import GalleryControls from './GalleryControls';
import GalleryImages from './GalleryImages';
import GalleryLists from './GalleryLists';
import Showcase from './Showcase';

AppRegistry.registerComponent('HelloWorld', () => App);
AppRegistry.registerComponent('Gallery', () => Gallery);
AppRegistry.registerComponent('GalleryLists', () => GalleryLists);
AppRegistry.registerComponent('GalleryImages', () => GalleryImages);
AppRegistry.registerComponent('GalleryControls', () => GalleryControls);
AppRegistry.registerComponent('Showcase', () => Showcase);
