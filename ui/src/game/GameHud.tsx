import { useEffect } from 'preact/hooks';
import { now, pauseOpen, scoreboardVisible } from '../store';
import { Chat } from './Chat';
import { PauseMenu, Scoreboard } from './Overlays';

export function GameHud() {
    // Coarse tick for chat fades; CEF only repaints what actually changed.
    useEffect(() => {
        const timer = window.setInterval(() => {
            now.value = Date.now();
        }, 500);
        return () => window.clearInterval(timer);
    }, []);
    return (
        <>
            <Chat />
            {scoreboardVisible.value && !pauseOpen.value && <Scoreboard />}
            {pauseOpen.value && <PauseMenu />}
        </>
    );
}
