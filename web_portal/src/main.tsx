import { render } from 'preact'
import './index.css'
import { App } from './app.tsx'

if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(err => {
      console.log('SW registration failed:', err);
    });
  });
}

render(<App />, document.getElementById('app')!)
