import type { InboundMessage, OutboundEvent } from './bridge';
import { type UiState, chatOpen, pauseOpen, gameplayMenu } from './store';

// Browser preview (npm run dev): stands in for the game so the screens can be
// designed without launching it. #menu or #game picks the screen.
export function startMock(receive: (message: InboundMessage) => void) {
    const view = location.hash.slice(1);
    const screen = view.startsWith('game') ? 'game' : 'menu';
    const base: UiState = {
        screen,
        version: 'dev',
        limits: { nickname: 24, message: 128 },
        connection: {
            phase: 'disconnected',
            status: 'Disconnected',
            active: false,
            playAvailable: false,
            host: '',
            port: 0,
            downloading: false,
            progress: 0,
            currentFile: '',
            filesDownloaded: 0,
            filesTotal: 0,
            bytesDownloaded: 0,
            bytesTotal: 0,
        },
        defaults: { nickname: 'Tommy', host: '127.0.0.1', port: 27015 },
        settings: {
            nickname: 'Tommy',
            recent: [
                { host: '127.0.0.1', port: 27015, name: '', lastUsed: Date.now() / 1000 - 3600 },
                { host: 'lost-heaven.example', port: 27015, name: '', lastUsed: Date.now() / 1000 - 86400 * 3 },
            ],
            favorites: [{ host: 'salieri.example', port: 27016, name: "Salieri's Bar", lastUsed: 0 }],
            preferences: {},
        },
    };
    if (view === 'menu-connected') {
        base.connection = { ...base.connection, phase: 'connected', status: 'Mission selected: freeride', active: true, playAvailable: true, host: '127.0.0.1', port: 27015 };
    }
    let current = base;
    const push = (next: UiState) => {
        current = next;
        receive({ type: 'state', payload: next });
    };
    setTimeout(() => push(base), 0);
    if (screen === 'game') {
        const lines = [
            { author: '', text: 'Welcome to Lost Heaven. Type /car to call a ride.', color: 0 },
            { author: 'Tommy', text: 'Anyone seen Paulie?', color: 0xe8c070ff },
            { author: 'Sam', text: 'He is down at the bar on Little Italy.', color: 0x7fb0e0ff },
            { author: '', text: 'Paulie joined the game', color: 0x9fd08aff },
        ];
        lines.forEach((line, index) => setTimeout(() => receive({ type: 'chat:message', payload: { ...line, time: Date.now() } }), 300 + index * 400));
        receive({
            type: 'players',
            payload: {
                mission: 'freeride',
                players: [
                    { id: 1, name: 'Tommy', health: 100, alive: true, spawned: true, local: true },
                    { id: 2, name: 'Sam', health: 64, alive: true, spawned: true, local: false },
                    { id: 3, name: 'Paulie', health: 0, alive: false, spawned: true, local: false },
                ],
            },
        });
        setTimeout(() => {
            if (view === 'game-chat') {
                receive({ type: 'chat:open', payload: { prefill: '/car ' } });
            } else if (view === 'game-pause') {
                receive({ type: 'pause:open', payload: {} });
            } else if (view === 'game-score') {
                receive({ type: 'scoreboard', payload: { visible: true } });
            }
        }, 2000);
        // Mirrors the client: Escape toggles the pause screen or closes chat.
        window.addEventListener('keydown', (event) => {
            if (gameplayMenu.value) return;
            if (event.key === 'Escape') {
                receive(chatOpen.value || pauseOpen.value ? { type: 'chat:closed', payload: {} } : { type: 'pause:open', payload: {} });
                return;
            }
            const tag = (event.target as HTMLElement | null)?.tagName;
            if (tag === 'INPUT') {
                return;
            }
            if (event.key === 't' || event.key === '/') {
                event.preventDefault();
                receive({ type: 'chat:open', payload: { prefill: event.key === '/' ? '/' : '' } });
            }
            else if (event.key === 'F1') {
                event.preventDefault();
                receive({ type: 'scoreboard', payload: { visible: true } });
            }
        });
        window.addEventListener('keyup', (event) => {
            if (event.key === 'F1') {
                receive({ type: 'scoreboard', payload: { visible: false } });
            }
        });
    }
    return (name: OutboundEvent, payload: unknown) => {
        if (name === 'chat:send') {
            receive({ type: 'chat:message', payload: { author: current.defaults.nickname, text: (payload as { text: string }).text, color: 0, time: Date.now() } });
        }
        else if (name === 'menu:connect') {
            const target = payload as { host: string; port: number };
            push({ ...current, connection: { ...current.connection, phase: 'connecting', status: 'Connecting...', active: true, host: target.host, port: target.port } });
            setTimeout(() => push({ ...current, connection: { ...current.connection, phase: 'downloading', status: 'Downloading server assets...', downloading: true, progress: 0.4 } }), 900);
            setTimeout(() => push({ ...current, connection: { ...current.connection, phase: 'connected', status: 'Connected', downloading: false, progress: 1, playAvailable: true } }), 2200);
        }
        else if (name === 'menu:disconnect') {
            push({ ...current, connection: { ...base.connection } });
            receive({ type: 'toast', payload: { kind: 'info', text: 'Disconnected' } });
        }
        else if (name === 'menu:play') {
            receive({ type: 'toast', payload: { kind: 'success', text: 'Entering Lost Heaven…' } });
        }
    };
}
