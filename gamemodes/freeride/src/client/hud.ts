// The Free Ride HUD: one transparent web view for toasts, the kill feed,
// activity cards, the race panel and the F2 city menu. F1 stays the mod's own
// player list and Tab the retail map. The server decides everything shown here; this script only relays its events,
// reads the replicated state bags and arbitrates the single native compass.
import type { Point } from './lib/geometry';
import { Page } from './lib/page';
import { fields } from './lib/payload';
import { onPicker } from './lib/picker';
import { events, onServer, onStart, onStop } from './lib/runtime';
import { Timers } from './lib/timers';

const MENU_REFRESH_MS = 3000;
const COMPASS_CHECK_MS = 500;
// Messages kept for a page that is not ready yet; the oldest go first.
const QUEUE_LIMIT = 60;
// The keys the help page names.
const HELP_KEYS = ['f2', 'f9', 'b', 'n', 'g', 'u'];

/** What the compass points at: a place, a player or a vehicle. */
interface CompassTarget {
    position?: Point;
    playerId?: number;
    vehicleId?: number;
}

interface CompassRequest {
    source: string;
    priority?: number;
    target: CompassTarget | null;
}

interface LevelUp {
    level: number;
    rank: string;
    promoted: boolean;
}

/** A model on Lucas Bertone's export list, named here from the stock catalog. */
interface ExportModel {
    model: string;
    name?: string;
}

/** The menu's sections, merged as the server sends them. */
interface MenuData {
    export?: { models?: ExportModel[] } | null;
    [section: string]: unknown;
}

const page = new Page('HUD view', 'hud.html', { visible: true, focus: false, zIndex: 70 }, () => {
    events.emitServer('fr:hud:ready', {});

    const queued = queue;

    queue = [];

    for (const [name, payload] of queued) {
        page.emit(name, payload ?? {});
    }

    lastPlayerState = '';
});

let queue: [name: string, payload: unknown][] = [];
let menuOpen = false;
let menuTab = 'city';
const menuRefresh = new Timers();
let catalogNames: Map<string, string> | null = null;
// The menu's sections so far (the server builds the costly ones only for
// their tab) and what the page was last given, so an unchanged refresh
// does not rebuild the whole panel.
let menuData: MenuData = {};
let menuPosted = '';
let pickerOpen = false;
let lastPlayerState = '';
const compass = new Map<string, { priority: number; target: CompassTarget }>();
let compassKey: string | null = null;
let compassCheckedAt = 0;

function post(name: string, payload?: unknown): void {
    if (!page.isReady) {
        queue.push([name, payload]);

        if (queue.length > QUEUE_LIMIT) {
            queue.shift();
        }

        return;
    }

    page.emit(name, payload ?? {});
}

function openView(): void {
    if (page.isOpen || !page.open()) {
        return;
    }

    page.on('hud:command', (request) => {
        const text = fields(request).text;

        if (typeof text !== 'string') {
            return;
        }

        events.emitServer('fr:menu:command', { text });
        setTimeout(requestMenu, 350);
    });

    page.on('hud:tab', (request) => {
        const tab = fields(request).tab;

        menuTab = typeof tab === 'string' ? tab : 'city';
        requestMenu();
    });

    page.on('hud:close', closeMenu);
    page.on('hud:settings', () => {
        closeMenu();
        Settings.open();
    });

    page.on('hud:car', () => {
        closeMenu();
        events.emitServer('fr:menu:command', { text: '/garage' });
    });
}

function closeView(): void {
    closeMenu();
    page.close();
    queue = [];
}

function requestMenu(): void {
    if (menuOpen) {
        events.emitServer('fr:menu:request', { tab: menuTab });
    }
}

function openMenu(tab?: string): void {
    if (!LocalPlayer?.spawned || pickerOpen) {
        return;
    }

    openView();

    if (tab) {
        menuTab = tab;
    }

    menuOpen = true;
    menuPosted = '';

    // The help page names the keys the player has now, not the defaults.
    const keys: Record<string, string> = {};

    for (const key of HELP_KEYS) {
        keys[key] = Settings.getKeyLabel(key);
    }

    post('menu:open', { tab: menuTab, keys });
    page.focus(true);
    requestMenu();
    menuRefresh.clear();
    menuRefresh.every(MENU_REFRESH_MS, requestMenu);
    Sound.play('00_kompas.wav', { volume: 0.35 });
}

function closeMenu(): void {
    if (!menuOpen) {
        return;
    }

    menuOpen = false;
    menuRefresh.clear();
    post('menu:close');
    page.focus(false);
}

// Several systems want the compass; the highest priority with a target the
// client can resolve wins.
function resolveTarget(target: CompassTarget): Point | Player | Vehicle | null {
    if (target.position) {
        return target.position;
    }

    if (typeof target.playerId === 'number') {
        return World.getPlayers().find((player) => player.id === target.playerId) ?? null;
    }

    if (typeof target.vehicleId === 'number') {
        return World.getVehicles().find((vehicle) => vehicle.id === target.vehicleId) ?? null;
    }

    return null;
}

function updateCompass(force: boolean): void {
    const now = Date.now();

    if (!force && now - compassCheckedAt < COMPASS_CHECK_MS) {
        return;
    }

    compassCheckedAt = now;

    let best: { priority: number; resolved: Point | Player | Vehicle; key: string } | null = null;

    for (const [source, entry] of compass) {
        const resolved = resolveTarget(entry.target);

        if (resolved && (!best || entry.priority > best.priority)) {
            best = {
                priority: entry.priority,
                resolved,
                key: `${source}:${JSON.stringify(entry.target)}`,
            };
        }
    }

    const key = best?.key ?? null;

    if (key === compassKey) {
        return;
    }

    compassKey = key;

    if (best) {
        Hud.setCompassTarget(best.resolved);
    } else {
        Hud.clearCompassTarget();
    }
}

// The export list names models by file; the stock catalog has their names.
function carNames(): Map<string, string> {
    if (!catalogNames?.size) {
        catalogNames = new Map(
            Scene.getCarCatalog().map((entry) => [entry.model.toLowerCase(), entry.name]),
        );
    }

    return catalogNames;
}

function relay(serverEvent: string, pageEvent = serverEvent.slice(3)): void {
    events.on(serverEvent, (data) => post(pageEvent, data));
}

relay('fr:toast');
relay('fr:feed');
relay('fr:card');
relay('fr:xp');
relay('fr:voice');
relay('fr:race:state', 'race');
relay('fr:profile');

onServer<LevelUp>('fr:levelup', (data) => {
    post('levelup', data);
    Sound.play('beep50.wav', { volume: 0.6 });
    Hud.announce(data.promoted ? data.rank.toUpperCase() : `LEVEL ${data.level}`, 3);
});

onServer<{ tab?: string }>('fr:menu:open', (request) => openMenu(request.tab));

onServer<MenuData>('fr:menu:data', (data) => {
    const names = carNames();

    for (const entry of data.export?.models ?? []) {
        entry.name = names.get(entry.model) ?? entry.model.replace(/00\.i3d$/i, '');
    }

    menuData = { ...menuData, ...data };

    const posted = JSON.stringify(menuData);

    if (posted === menuPosted) {
        return;
    }

    menuPosted = posted;
    post('menu:data', menuData);
});

onServer<CompassRequest>('fr:compass', (request) => {
    if (typeof request.source !== 'string') {
        return;
    }

    if (request.target) {
        compass.set(request.source, { priority: request.priority ?? 0, target: request.target });
    } else {
        compass.delete(request.source);
    }

    updateCompass(true);
});

function postVoice(talking: boolean): void {
    if (LocalPlayer) {
        post('voice', { id: LocalPlayer.id, name: LocalPlayer.nickname, talking });
    }
}

events.on('voiceStart', () => postVoice(true));
events.on('voiceStop', () => postVoice(false));

onPicker((open) => {
    pickerOpen = open;

    if (pickerOpen) {
        closeMenu();
    }

    post('visibility', { hidden: pickerOpen });
});

Settings.describeKey('f2', 'City menu');
Key.bind('f2', () => {
    if (menuOpen) {
        closeMenu();
    } else {
        openMenu();
    }
});

events.on('missionReady', () => {
    openView();
    compassKey = null;
    updateCompass(true);
});

events.on('missionUnload', () => {
    closeMenu();
    menuData = {};
    compass.clear();
    compassKey = null;
    post('reset');
});

onStart(() => {
    if (World.isReady()) {
        openView();
    }
});

onStop(closeView);

events.on('render', () => {
    if (!page.isOpen) {
        return;
    }

    const player = LocalPlayer;
    const state = `${Boolean(player?.spawned)}:${Boolean(player?.alive)}`;

    if (state !== lastPlayerState && page.isReady) {
        lastPlayerState = state;
        post('player', {
            spawned: Boolean(player?.spawned),
            alive: Boolean(player?.alive),
            id: player?.id ?? null,
        });

        if (!player?.alive) {
            closeMenu();
        }
    }

    updateCompass(false);
});
