import '@fontsource/eb-garamond/400.css';
import '@fontsource/eb-garamond/400-italic.css';
import '@fontsource/limelight/400.css';
import '@fontsource/playfair-display/400.css';
import '@fontsource/playfair-display/700.css';
import '@fontsource/playfair-display/400-italic.css';
import './global.css';

import { render } from 'preact';
import { App } from './App';
import { startBridge } from './bridge';
import { Cursor } from './components/Cursor';

render(location.hash === '#cursor' ? <Cursor /> : <App />, document.getElementById('app') as HTMLElement);
void startBridge();
