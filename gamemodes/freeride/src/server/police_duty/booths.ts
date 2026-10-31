// The city's telephone booths (data/phone_booths.js), from which an officer
// on duty rings the precinct. Only officers on duty see them: a marker over
// each and, near one, the call prompt (client/police.ts).
import booths from '../data/phone_booths.js';
import * as maps from '../lib/maps';
import { markers } from '../lib/notify';
import { distance, emit, flatDistance, type Point } from '../lib/util';
import { PlayerSet } from '../lib/players';

// A booth is in reach this close to its frame, on the same level.
export const BOOTH_REACH = 2.8;
const BOOTH_LEVEL = 2.5;
const BOOTH_MARKER_RANGE = 160;

export const CALL_COLOR = '#6fa8ff';

export interface Booth {
    frame: string;
    position: Point;
}

/** What client/police.ts does on the server's word: shows a booth, plays a sound. */
export type PoliceRadio =
    | { action: 'booths'; reach?: number; booths: Point[] }
    | { action: 'sound'; wave: string; volume: number };

const officersWithBooths = new PlayerSet({ clearOnMissionChange: true });

export function sendPolice(player: Player, data: PoliceRadio): void {
    emit(player, 'fr:police', data);
}

function boothList(): Booth[] {
    const map = maps.current();

    return (map ? (booths[map.id] ?? []) : []).map(([frame, x, y, z]) => ({
        frame,
        position: { x, y, z },
    }));
}

/** The booth the player can call from, if any: the nearest in reach. */
export function boothAt(position: Point): (Booth & { gap: number }) | null {
    let best: (Booth & { gap: number }) | null = null;

    for (const booth of boothList()) {
        const gap = flatDistance(booth.position, position);

        if (
            gap <= BOOTH_REACH &&
            Math.abs(booth.position.y - position.y) <= BOOTH_LEVEL &&
            (!best || gap < best.gap)
        ) {
            best = { ...booth, gap };
        }
    }

    return best;
}

export function nearestBooth(position: Point): (Booth & { gap: number }) | null {
    let best: (Booth & { gap: number }) | null = null;

    for (const booth of boothList()) {
        const gap = distance(booth.position, position);

        if (!best || gap < best.gap) {
            best = { ...booth, gap };
        }
    }

    return best;
}

// Booth markers and the call prompt, for officers on duty only.
export function showBooths(player: Player): void {
    const list = boothList();

    officersWithBooths.add(player.id);
    markers(
        player,
        'booths',
        list.map((booth) => ({
            id: booth.frame,
            position: booth.position,
            label: 'CALL BOX',
            color: CALL_COLOR,
            ring: 1.2,
            range: BOOTH_MARKER_RANGE,
        })),
    );

    sendPolice(player, {
        action: 'booths',
        reach: BOOTH_REACH,
        booths: list.map((booth) => booth.position),
    });
}

export function hideBooths(player: Player): void {
    if (!officersWithBooths.delete(player.id)) {
        return;
    }

    markers(player, 'booths', []);
    sendPolice(player, { action: 'booths', booths: [] });
}
