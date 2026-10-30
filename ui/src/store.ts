import { computed, signal } from '@preact/signals';

export type Screen = 'hidden' | 'menu' | 'game' | 'loading';
export type Phase = 'disconnected' | 'connecting' | 'authenticating' | 'downloading' | 'starting' | 'connected';

export interface CursorState {
    x: number;
    y: number;
    pressed: boolean;
    shape: 'arrow' | 'text' | 'pointer' | 'move' | 'wait' | 'blocked' | 'none';
}

export const cursor = signal<CursorState | null>(null);

export interface ServerEntry {
    host: string;
    port: number;
    name: string;
    lastUsed: number;
}

export interface Preferences {
    chatFadeSeconds: number;
    chatScale: number;
    uiScale: number;
    chatTimestamps: boolean;
    filmGrain: boolean;
    reduceMotion: boolean;
}

export interface UiState {
    screen: Screen;
    version: string;
    limits: { nickname: number; message: number };
    connection: {
        phase: Phase;
        status: string;
        active: boolean;
        playAvailable: boolean;
        host: string;
        port: number;
        downloading: boolean;
        progress: number;
        currentFile: string;
        filesDownloaded: number;
        filesTotal: number;
        bytesDownloaded: number;
        bytesTotal: number;
    };
    defaults: { nickname: string; host: string; port: number };
    settings: {
        nickname: string;
        recent: ServerEntry[];
        favorites: ServerEntry[];
        preferences: Partial<Preferences>;
    };
}

export interface ChatLine {
    id: number;
    author: string;
    text: string;
    color: number;
    time: number;
}

export interface Player {
    id: number;
    name: string;
    health: number;
    alive: boolean;
    spawned: boolean;
    // The server's round trip to the player, in milliseconds.
    ping: number;
    local: boolean;
}

export interface Toast {
    id: number;
    kind: 'info' | 'success' | 'error';
    text: string;
}

export const defaultPreferences: Preferences = {
    chatFadeSeconds: 12,
    chatScale: 1,
    uiScale: 1,
    chatTimestamps: false,
    filmGrain: true,
    reduceMotion: false,
};

export const state = signal<UiState>({
    screen: 'hidden',
    version: '',
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
    defaults: { nickname: 'Player', host: '127.0.0.1', port: 27015 },
    settings: { nickname: '', recent: [], favorites: [], preferences: {} },
});

export const preferences = computed<Preferences>(() => ({ ...defaultPreferences, ...state.value.settings.preferences }));

export const chat = signal<ChatLine[]>([]);
export const chatOpen = signal<{ prefill: string } | null>(null);
export const pauseOpen = signal(false);
export interface GameplayMenu {
    id: number;
    kind: 'inventory' | 'interaction';
    title: string;
    choices: { label: string; detail: string; canSelect: boolean; canDrop: boolean }[];
}
export const gameplayMenu = signal<GameplayMenu | null>(null);
export const scoreboardVisible = signal(false);
export const roster = signal<{ players: Player[]; mission: string }>({ players: [], mission: '' });
export const toasts = signal<Toast[]>([]);

// The client's settings screen (code/client/src/features/settings). Slots are
// "native:<retail slot>", "mod:<key>" and "script:<resource>/<key>".
export interface SettingsSlot {
    key: string;
    label: string;
    defaultLabel: string;
    mouse: boolean;
    wheel: boolean;
    clearable: boolean;
    conflicts: string[];
}

export interface SettingsGroup {
    id: string;
    label: string;
    rows: { id: string; label: string; slots: string[] }[];
}

export interface GameSettings {
    groups: SettingsGroup[];
    slots: Record<string, SettingsSlot>;
    mouse: {
        sensitivityX: number;
        sensitivityY: number;
        turnLimit: number;
        steering: number;
        invertY: boolean;
        doubleTapRoll: boolean;
        mph: boolean;
        crosshair: number;
    };
    audio: { sound: number; music: number; car: number; speech: number };
    media: number;
    voice: {
        enabled: boolean;
        volume: number;
        mode: 'pushToTalk' | 'voiceActivity';
        threshold: number;
        releaseMs: number;
        noiseSuppression: boolean;
        device: string;
    };
    hud: { nametags: boolean; nametagHealth: boolean };
    devices: string[];
    voiceAvailable: boolean;
    toggles: { resource: string; id: string; label: string; value: boolean }[];
    // True while that part still follows the selected Mafia profile.
    profile: { controls: boolean; audio: boolean };
}

export interface VoiceLevel {
    level: number;
    talking: boolean;
    microphone: boolean;
}

// A server mission loading (the client's `loading` message). Progress is the
// retail loader's share of the whole load, or -1 before it reports any.
export interface LoadingState {
    mission: string;
    progress: number;
    stage: 'preparing' | 'scene' | 'world';
}

export const loading = signal<LoadingState>({ mission: '', progress: -1, stage: 'preparing' });

export const settingsOpen = signal(false);
export const gameSettings = signal<GameSettings | null>(null);
export const voiceLevel = signal<VoiceLevel>({ level: 0, talking: false, microphone: false });

// Wall clock for fades, ticked by the HUD while it is on screen.
export const now = signal(Date.now());

let nextId = 1;
export function uid(): number {
    return nextId++;
}

export const kMaxChatLines = 100;

export function pushChat(line: Omit<ChatLine, 'id'>) {
    const lines = [...chat.value, { ...line, id: uid() }];
    chat.value = lines.length > kMaxChatLines ? lines.slice(lines.length - kMaxChatLines) : lines;
}

export function pushToast(kind: Toast['kind'], text: string) {
    const id = uid();
    toasts.value = [...toasts.value.slice(-3), { id, kind, text }];
    window.setTimeout(() => {
        toasts.value = toasts.value.filter((toast) => toast.id !== id);
    }, 4800);
}

// Code points, not UTF-16 units: the server counts code points too.
export function codePoints(text: string): number {
    return Array.from(text).length;
}

export function clampCodePoints(text: string, limit: number): string {
    const points = Array.from(text);
    return points.length > limit ? points.slice(0, limit).join('') : text;
}
