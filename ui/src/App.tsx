import * as stylex from '@stylexjs/stylex';
import { useEffect } from 'preact/hooks';
import { send } from './bridge';
import { Toasts } from './components/Toasts';
import { GameHud } from './game/GameHud';
import { LoadingScreen } from './screens/LoadingScreen';
import { MainMenu } from './screens/MainMenu';
import { chatOpen, pauseOpen, gameplayMenu, loading, preferences, settingsOpen, state } from './store';
import { colors, fonts } from './tokens.stylex';

const styles = stylex.create({
    root: {
        position: 'fixed',
        inset: 0,
        overflow: 'hidden',
        color: colors.text,
        fontFamily: fonts.sans,
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
            settingsOpen.value = false;
        }
        // The client sends a fresh report each time a load starts.
        if (screen !== 'loading') {
            loading.value = { mission: '', progress: -1, stage: 'preparing' };
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
            {screen === 'loading' && <LoadingScreen />}
            {screen !== 'hidden' && <Toasts />}
        </div>
    );
}
