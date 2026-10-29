import type { InboundMessage, OutboundEvent } from './bridge';
import { type GameSettings, type UiState, chatOpen, pauseOpen, gameplayMenu } from './store';

function mockSettings(): GameSettings {
    const slot = (key: string, label: string, conflicts: string[] = []) => ({ key, label, defaultLabel: label, mouse: true, wheel: true, clearable: true, conflicts });
    return {
        groups: [
            { id: 'onFoot', label: 'On foot', rows: [{ id: 'forward', label: 'Move forward', slots: ['native:forward', 'native:forward2'] }, { id: 'use', label: 'Use / enter car', slots: ['native:use', 'native:use2'] }] },
            { id: 'car', label: 'In a car', rows: [{ id: 'horn', label: 'Horn', slots: ['native:horn', 'native:horn2'] }] },
            { id: 'multiplayer', label: 'Multiplayer', rows: [{ id: 'chat', label: 'Chat', slots: ['mod:chat'] }, { id: 'pushToTalk', label: 'Voice: push to talk', slots: ['mod:pushToTalk'] }] },
            { id: 'script:freeride', label: 'Server: freeride', rows: [{ id: 'f9', label: 'Car radio on or off', slots: ['script:freeride/f9'] }] },
        ],
        slots: {
            'native:forward': slot('up', 'Up'),
            'native:forward2': slot('w', 'W'),
            'native:use': slot('mouse2', 'Mouse 2'),
            'native:use2': slot('u', 'U', ['Police: call for backup']),
            'native:horn': slot('k', 'K', ['Siren (police car)']),
            'native:horn2': slot('', ''),
            'mod:chat': { ...slot('t', 'T'), mouse: false, wheel: false, clearable: false },
            'mod:pushToTalk': { ...slot('v', 'V'), wheel: false },
            'script:freeride/f9': { ...slot('f9', 'F9'), wheel: false },
        },
        mouse: { sensitivityX: 0.5, sensitivityY: 0.5, turnLimit: 0.5, steering: 0.2, invertY: false, doubleTapRoll: true, mph: true, crosshair: 0 },
        audio: { sound: 1, music: 0.8, car: 1, speech: 1 },
        media: 1,
        voice: { enabled: true, volume: 1, mode: 'pushToTalk', threshold: 0.03, releaseMs: 100, noiseSuppression: false, device: '' },
        hud: { nametags: true, nametagHealth: true },
        devices: ['Microphone (USB Audio)', 'Headset Microphone'],
        voiceAvailable: true,
        toggles: [{ resource: 'freeride', id: 'radio', label: 'Car radio', value: true }],
        profile: { controls: true, audio: false },
    };
}

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
    let settings = mockSettings();
    return (name: OutboundEvent, payload: unknown) => {
        if (name === 'settings:open' || name === 'settings:devices') {
            receive({ type: 'settings', payload: settings });
        }
        else if (name === 'settings:set') {
            const patch = payload as Partial<GameSettings>;
            settings = {
                ...settings,
                mouse: { ...settings.mouse, ...patch.mouse },
                audio: { ...settings.audio, ...patch.audio },
                voice: { ...settings.voice, ...patch.voice },
                hud: { ...settings.hud, ...patch.hud },
                media: patch.media ?? settings.media,
            };
            receive({ type: 'settings', payload: settings });
        }
        else if (name === 'settings:bind') {
            const { slot, key } = payload as { slot: string; key: string };
            settings = { ...settings, slots: { ...settings.slots, [slot]: { ...settings.slots[slot], key, label: key.toUpperCase() } } };
            receive({ type: 'settings', payload: settings });
        }
        else if (name === 'settings:reset') {
            settings = mockSettings();
            receive({ type: 'settings', payload: settings });
        }
        else if (name === 'chat:send') {
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
