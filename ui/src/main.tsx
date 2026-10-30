import '@fontsource/inter/400.css';
import '@fontsource/inter/500.css';
import '@fontsource/inter/600.css';
import '@fontsource/playfair-display/700.css';
import './global.css';

import { render } from 'preact';
import { App } from './App';
import { startBridge } from './bridge';
import { Cursor } from './components/Cursor';

render(location.hash === '#cursor' ? <Cursor /> : <App />, document.getElementById('app') as HTMLElement);
void startBridge();
