// Presentation requests for client/hud.ts. The server decides what happened;
// clients only render toasts, the feed, activity cards, markers and compass.
import { emit, broadcast, type Point } from './util';
import { events } from './lifecycle';
import type { SentCache } from './players';

export const COLORS = Object.freeze({
    gold: '#e2b866',
    red: '#d0574a',
    green: '#8fc27a',
    blue: '#8fb4d8',
    muted: '#b9ae98',
    white: '#f3e9d6',
});

export type Tone = 'info' | 'good' | 'bad' | 'gold';

export interface ToastOptions {
    title?: string;
    tone?: Tone;
    seconds?: number;
}

export function toast(
    player: Player,
    text: string,
    { title = '', tone = 'info', seconds = 5 }: ToastOptions = {},
): void {
    emit(player, 'fr:toast', { title, text, tone, seconds });
}

export function toastAll(
    text: string,
    { title = '', tone = 'info', seconds = 5 }: ToastOptions = {},
): void {
    broadcast('fr:toast', { title, text, tone, seconds });
}

/** One coloured segment of a feed line. */
export interface FeedPart {
    t: string;
    c: string;
}

export function name(
    player: { nickname: string } | null | undefined,
    color: string = COLORS.white,
): FeedPart {
    return { t: player?.nickname ?? 'Someone', c: color };
}

export function text(value: unknown, color: string = COLORS.muted): FeedPart {
    return { t: String(value), c: color };
}

export function feed(kind: string, parts: FeedPart[]): void {
    broadcast('fr:feed', { kind, parts });
}

/** An activity card on the right of the HUD. */
export interface Card {
    title: string;
    subtitle?: string;
    rows?: [string, string][];
    tone?: Tone;
    [extra: string]: unknown;
}

// Cards, the compass and markers are refreshed on timers, so one that has not
// changed is not sent again (a marker list resent also reloads its models).
// The client forgets them with its HUD page or the mission, and so does this.
const lastCards = new Map<string, string>();
const lastCompass = new Map<string, string>();
const lastMarkers = new Map<string, string>();

function forget(sent: Map<string, string>, player: Player): void {
    for (const key of [...sent.keys()]) {
        if (key.startsWith(`${player.id}:`)) {
            sent.delete(key);
        }
    }
}

function forgetAll(player: Player): void {
    forget(lastCards, player);
    forget(lastCompass, player);
    forget(lastMarkers, player);
}

function sendOnce(
    sent: Map<string, string>,
    player: Player,
    key: string,
    eventName: string,
    data: unknown,
): void {
    const payload = JSON.stringify(data);

    if (sent.get(key) === payload) {
        return;
    }

    sent.set(key, payload);
    player.emit(eventName, payload);
}

// A freshly loaded HUD page shows no cards, whatever was sent before; a
// mission load drops the markers, a spawn the compass.
events.onClient('fr:hud:ready', (player) => {
    if (player instanceof Player) {
        forgetAll(player);
    }
});

events.on('playerMissionReady', (player) => {
    forget(lastMarkers, player);
    forget(lastCompass, player);
});

events.on('playerSpawn', (player) => forget(lastCompass, player));
events.on('playerDisconnect', forgetAll);
events.on('missionChange', () => {
    lastCards.clear();
    lastCompass.clear();
    lastMarkers.clear();
});

/** Shows, replaces or (with null) removes one of the player's activity cards. */
export function card(player: Player, id: string, content: Card | null): void {
    sendOnce(
        lastCards,
        player,
        `${player.id}:${id}`,
        'fr:card',
        content ? { id, ...content } : { id, remove: true },
    );
}

/** What the compass points at: a place, a player or a vehicle. */
export type CompassTarget = { position: Point } | { playerId: number } | { vehicleId: number };

/** Points the compass for `source`; the highest priority source wins. null stops it. */
export function compass(
    player: Player,
    source: string,
    priority: number,
    target: CompassTarget | null,
): void {
    sendOnce(lastCompass, player, `${player.id}:${source}`, 'fr:compass', {
        source,
        priority,
        target: target ?? null,
    });
}

/** A marker the client draws: a beacon, label or model in the world and, unless map is false, a city-map blip. */
export interface Marker {
    id: string;
    position: Point;
    label?: string;
    color?: string;
    [extra: string]: unknown;
}

/** A marker that follows a vehicle wherever the client streams it. */
export interface VehicleMarker {
    id: string;
    vehicleId: number;
    /** Where it was, for a client that does not stream the vehicle. */
    position?: Point;
    label?: string;
    color?: string;
    [extra: string]: unknown;
}

/** The stock arrow (sipka) over a door or a way out, seen from `range` metres. */
export function arrow(
    id: string,
    position: Point,
    label: string,
    color: string,
    range: number,
    extra: Record<string, unknown> = {},
): Marker {
    return { id, position, label, color, model: 'sipka.i3d', range, ...extra };
}

export function markers(
    player: Player,
    group: string,
    items: readonly (Marker | VehicleMarker)[] | null,
): void {
    sendOnce(lastMarkers, player, `${player.id}:${group}`, 'fr:markers', {
        group,
        items: items ?? [],
    });
}

export function markersAll(group: string, items: readonly (Marker | VehicleMarker)[] | null): void {
    for (const key of [...lastMarkers.keys()]) {
        if (key.endsWith(`:${group}`)) {
            lastMarkers.delete(key);
        }
    }

    broadcast('fr:markers', { group, items: items ?? [] });
}

/**
 * Markers whose city-map blips only a crew sees: everyone gets the world
 * markers, the crew also gets the blips. `sent` keeps each player from being
 * sent the same list twice; call it every tick, so duty changes show within one.
 */
export function crewMarkers(
    group: string,
    sent: SentCache,
    items: Marker[],
    isCrew: (player: Player) => boolean,
): void {
    const publicItems = items.map((item) => ({ ...item, map: false }));
    const crewPayload = JSON.stringify(items);
    const publicPayload = JSON.stringify(publicItems);

    for (const player of World.getPlayers()) {
        const crew = isCrew(player);

        if (sent.changed(player, crew ? crewPayload : publicPayload)) {
            markers(player, group, crew ? items : publicItems);
        }
    }
}

export function announce(
    player: Player,
    message: string,
    seconds = 2,
    sound: string | null = null,
): void {
    emit(player, 'fr:announce', { text: message, seconds, sound });
}
