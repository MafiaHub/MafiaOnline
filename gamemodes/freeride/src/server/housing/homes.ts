// Who owns which home and who may come in. The homes are one saved document
// (lib/store.ts "housing"); an owner is a record key (lib/profiles.ts).
import * as store from '../lib/store';
import * as profiles from '../lib/profiles';
import * as decor from '../lib/house_decor';
import { here, type House } from './catalogue';
import type { CrewTag } from '../lib/nametag';
import type { RoomKey } from '../lib/house_rooms';

export const MAX_HOMES = 2;
const SELL_FRACTION = 0.6;
const ROSTER_MS = 10_000;

/**
 * A decoration, kept as the floor point under its box centre (room-local
 * x, z), its height above the floor and its yaw.
 */
export interface DecorEntry {
    /** Its number within the home. */
    n: number;
    /** The lib/house_decor.ts item id. */
    item: string;
    room: RoomKey;
    x: number;
    z: number;
    lift: number;
    yaw: number;
}

export interface Home {
    /** The owner's record key. */
    owner: string;
    ownerName: string;
    /** What the owner paid; the price when the document lacks it. */
    paid?: number;
    bought: number;
    locked: boolean;
    /** The crew tag let in, or null. */
    crew: string | null;
    /** Record key -> nickname. */
    guests: Record<string, string>;
    decor: DecorEntry[];
    /** The number the next decoration gets. */
    next: number;
}

// A home as an earlier version may have saved it.
type StoredHome = Omit<Home, 'guests' | 'decor'> & Partial<Pick<Home, 'guests' | 'decor'>>;

interface HousingDocument {
    version: 1;
    homes: Record<string, StoredHome>;
}

interface CrewsDocument {
    crews?: { tag: string; members?: string[] }[];
}

// By house id.
let homes: Record<string, Home> = {};

export function load(): void {
    const stored = store.load<HousingDocument>('housing')?.homes ?? {};

    homes = Object.fromEntries(
        Object.entries(stored).map(([id, home]) => [
            id,
            { ...home, guests: home.guests ?? {}, decor: home.decor ?? [] },
        ]),
    );
}

export function save(): void {
    store.save('housing', { version: 1, homes });
}

export function homeOf(house: House): Home | null {
    return homes[house.id] ?? null;
}

export function setHome(house: House, home: Home): void {
    homes[house.id] = home;
}

export function removeHome(house: House): void {
    delete homes[house.id];
}

export function keyOf(player: Player): string | null {
    return profiles.keyOf(player);
}

/** The player's homes on this map. */
export function ownedBy(player: Player): House[] {
    const key = keyOf(player);

    return key ? here().filter((house) => homes[house.id]?.owner === key) : [];
}

/** How many homes the player owns on every map. */
export function ownedAnywhere(player: Player): number {
    const key = keyOf(player);

    return key ? Object.values(homes).filter((home) => home.owner === key).length : 0;
}

export function isOwner(player: Player, house: House): boolean {
    const key = keyOf(player);

    return Boolean(key && homes[house.id]?.owner === key);
}

/** The owner, when they are online. */
export function ownerOnline(home: Home): Player | null {
    return World.getPlayers().find((candidate) => keyOf(candidate) === home.owner) ?? null;
}

/** The tag of the crew the player is in, from their public `fr.crew`. */
export function crewTagOf(player: Player): string | null {
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');

    return crew?.tag ?? null;
}

// The crew the owner is in now: from their state bag when online, from the
// crews' saved roster otherwise (lib/store.ts "crews", members by account
// name), read at most every ten seconds. A home opened to a crew stays shut
// to it once its owner has left that crew.
let roster: { at: number; crews: NonNullable<CrewsDocument['crews']> } = { at: 0, crews: [] };

function ownerCrew(home: Home): string | null {
    const online = ownerOnline(home);

    if (online) {
        return crewTagOf(online);
    }

    if (Date.now() - roster.at > ROSTER_MS) {
        roster = { at: Date.now(), crews: store.load<CrewsDocument>('crews')?.crews ?? [] };
    }

    const name = String(home.ownerName ?? '').toLowerCase();

    return roster.crews.find((crew) => (crew.members ?? []).includes(name))?.tag ?? null;
}

export function mayEnter(player: Player, house: House): boolean {
    const home = homes[house.id];

    if (!home) {
        return false;
    }

    const key = keyOf(player);

    if (home.owner === key || !home.locked || (key && home.guests[key])) {
        return true;
    }

    const tag = crewTagOf(player);

    return Boolean(tag && home.crew && tag === home.crew && ownerCrew(home) === home.crew);
}

/** What the home's furnishings are worth: they sell back at full price. */
function decorValue(home: Home): number {
    return home.decor.reduce((sum, entry) => sum + (decor.byId(entry.item)?.price ?? 0), 0);
}

/** What selling the home back pays: 60% of its price and the furnishings at full value. */
export function refundFor(house: House, home: Home): number {
    return Math.round((home.paid ?? house.price) * SELL_FRACTION) + decorValue(home);
}
