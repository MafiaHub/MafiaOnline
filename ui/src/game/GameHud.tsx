import { useEffect } from 'preact/hooks';
import { now, pauseOpen, scoreboardVisible, gameplayMenu } from '../store';
import { Chat } from './Chat';
import { PauseMenu, Scoreboard } from './Overlays';
import { GameplayMenu } from './GameplayMenu';

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
            {scoreboardVisible.value && !pauseOpen.value && !gameplayMenu.value && <Scoreboard />}
            {pauseOpen.value && <PauseMenu />}
            {gameplayMenu.value && <GameplayMenu key={gameplayMenu.value.id} menu={gameplayMenu.value} />}
        </>
    );
}
