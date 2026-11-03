// Who is in which home, and which room of it. A player inside stands in the
// home's own virtual world over the shared rooms (lib/house_rooms.ts).
import * as life from '../lib/life';
import { toast } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { Transfers } from '../lib/transfer';
import { flatDistance } from '../lib/util';
import { ALL, type House } from './catalogue';
import { isOwner, ownedBy } from './homes';
import type { RoomKey } from '../lib/house_rooms';

// Activities a home is no place for.
const BLOCKING_ACTIVITIES: ReadonlySet<string> = new Set(['race', 'race-lobby', 'duel', 'jail']);

export interface Whereabouts {
    houseId: string;
    room: RoomKey;
}

/** Players inside a home; a mission change empties it (housing/index.ts). */
export const inside = new PlayerMap<Whereabouts>();

/** Moves in and out of the homes. */
export const moves = new Transfers();

export function insideHouse(player: Player): House | null {
    const entry = inside.get(player.id);

    return entry ? (ALL.get(entry.houseId) ?? null) : null;
}

export function roomOf(player: Player): RoomKey | null {
    return inside.get(player.id)?.room ?? null;
}

export function occupants(house: House): Player[] {
    return World.getPlayers().filter((player) => inside.get(player.id)?.houseId === house.id);
}

/** Why the player may not go in or take a car out now, or null. */
export function blocked(player: Player): string | null {
    const current = life.activity(player);

    if (current && BLOCKING_ACTIVITIES.has(current.kind)) {
        return life.busyMessage(player);
    }

    if ((Police.getRecord(player)?.level ?? 0) > 0) {
        return 'Not with the police on your tail.';
    }

    return null;
}

/** The home a command means: the one named, the one the player is in, or their nearest. */
function target(player: Player, id: string | undefined): House | null {
    if (id) {
        return ALL.get(id) ?? null;
    }

    return (
        insideHouse(player) ??
        ownedBy(player).sort(
            (a, b) => flatDistance(player.position, a.door) - flatDistance(player.position, b.door),
        )[0] ??
        null
    );
}

/** target(), when it is the player's own; otherwise they are told and it is null. */
export function mine(player: Player, id: string | undefined): House | null {
    const house = target(player, id);

    if (!house || !isOwner(player, house)) {
        toast(player, 'That is not one of your homes.', { tone: 'bad' });

        return null;
    }

    return house;
}
