import * as stylex from '@stylexjs/stylex';
import { useEffect } from 'preact/hooks';
import { send } from './bridge';
import { Toasts } from './components/Toasts';
import { GameHud } from './game/GameHud';
import { MainMenu } from './screens/MainMenu';
import { chatOpen, pauseOpen, gameplayMenu, preferences, state } from './store';

const styles = stylex.create({
    root: {
        position: 'fixed',
        inset: 0,
        overflow: 'hidden',
    },
});

export function App() {
    const screen = state.value.screen;
    const prefs = preferences.value;

    // The client composites this page only once it confirms it has drawn the
    // screen the client asked for, so a stale menu never covers the game.
    useEffect(() => {
        if (screen !== 'game') {
            gameplayMenu.value = null;
            chatOpen.value = null;
            pauseOpen.value = false;
        }
        let second = 0;
        const first = requestAnimationFrame(() => {
            second = requestAnimationFrame(() => send('ui:screen', screen));
        });
        return () => {
            cancelAnimationFrame(first);
            cancelAnimationFrame(second);
        };
    }, [screen]);

    useEffect(() => {
        const root = document.documentElement;
        root.style.setProperty('--ui-scale', String(prefs.uiScale));
        root.toggleAttribute('data-reduce-motion', prefs.reduceMotion);
    }, [prefs.uiScale, prefs.reduceMotion]);

    return (
        <div {...stylex.props(styles.root)}>
            {screen === 'menu' && <MainMenu />}
            {screen === 'game' && <GameHud />}
            {screen !== 'hidden' && <Toasts />}
        </div>
    );
}
