// The bars of each map: their rooms, counters, doors and the people in them.
// Positions are the bar's own frames in the story missions; headings are
// the frames' rotations (the engine turns by the conjugate quaternion).
import * as barRoom from '../lib/bar_room';
import * as maps from '../lib/maps';
import { MAIN_WORLD, type Placement, type Point } from '../lib/util';

export const ROUND_PER_HEAD = 4;

export interface Drink {
    id: string;
    name: string;
    /** Null for the round, which costs ROUND_PER_HEAD a guest. */
    price: number | null;
    /** What it adds to drunkenness (server/drunk.ts); coffee takes some off. */
    strength: number;
    health: number;
    note: string;
}

// Prices for a city where a day's odd jobs pay $30 an hour.
export const DRINKS: readonly Drink[] = [
    {
        id: 'beer',
        name: 'Beer',
        price: 4,
        strength: 10,
        health: 2,
        note: 'Cold, from the back room. +2 health',
    },
    {
        id: 'wine',
        name: 'Red wine',
        price: 6,
        strength: 14,
        health: 3,
        note: 'Chianti, for the old country. +3 health',
    },
    {
        id: 'gin',
        name: 'Bathtub gin',
        price: 7,
        strength: 18,
        health: 3,
        note: 'Nobody asks where it was made. +3 health',
    },
    {
        id: 'whiskey',
        name: 'Canadian whiskey',
        price: 10,
        strength: 22,
        health: 5,
        note: 'Came over the lake last night. +5 health',
    },
    {
        id: 'grappa',
        name: 'The house grappa',
        price: 15,
        strength: 32,
        health: 6,
        note: 'Strong enough to strip paint. +6 health',
    },
    {
        id: 'coffee',
        name: 'Black coffee',
        price: 3,
        strength: -15,
        health: 4,
        note: 'Sobers you up a little. +4 health',
    },
    {
        id: 'round',
        name: 'A round for the house',
        price: null,
        strength: 10,
        health: 2,
        note: `A beer for everyone in the bar · $${ROUND_PER_HEAD} a head`,
    },
];

/** Clips a drinker or barman plays in turn, each for its time; a lone clip loops. */
export type Routine = readonly (readonly [clip: string, ms: number])[];

export interface StaffSpec extends Placement {
    model: string;
    routine: Routine;
    /** Holds a glass: drinks to a round. */
    glass?: boolean;
}

interface Box {
    x0: number;
    x1: number;
    z0: number;
    z1: number;
    y0: number;
    y1: number;
}

export interface Bar {
    id: 'salieri' | 'morello' | 'backroom';
    name: string;
    tagline: string;
    world: number;
    /** Built from map objects (lib/bar_room.ts): staffed only once it stands. */
    built?: boolean;
    /** The rooms guests drink in; the back room's own when null. */
    rooms: readonly Box[] | null;
    /** Where customers order, and how far from it they may stand. */
    counter: { position: Point; reach: number };
    /** Where its name hangs over the street, if anywhere. */
    door: Point | null;
    /** The pavement a guest who blacked out wakes on. */
    wake: Placement | null;
    bartender: StaffSpec;
    patrons: readonly StaffSpec[];
}

export const BARMAN: Routine = [
    ['x barman stativ.i3d', 7000],
    ['x barman stul.i3d', 5000],
    ['x barman stativ.i3d', 7000],
    ['x barman umyvani.i3d', 6000],
];
export const SERVING = 'x barman stul.i3d';
const GLASS: Routine = [
    ['GestSklStativ.i3d', 8000],
    ['GestSklNapitise.i3d', 4000],
    ['GestSklStativ.i3d', 6000],
    ['GestSkl01.i3d', 4000],
];

export const CHEERS = 'GestSklPripiti.i3d';

function box(x0: number, x1: number, z0: number, z1: number, y0: number, y1: number): Box {
    return { x0, x1, z0, z1, y0, y1 };
}

function inBoxes(boxes: readonly Box[], position: Point): boolean {
    return boxes.some(
        (b) =>
            position.x >= b.x0 &&
            position.x <= b.x1 &&
            position.z >= b.z0 &&
            position.z <= b.z1 &&
            position.y >= b.y0 &&
            position.y <= b.y1,
    );
}

export function contains(bar: Bar, position: Point): boolean {
    return bar.rooms ? inBoxes(bar.rooms, position) : barRoom.inRoom(position, 0.5);
}

const SALIERI: Bar = {
    id: 'salieri',
    name: "Salieri's Bar",
    tagline: 'Family business. Keep your voice down',
    world: MAIN_WORLD,
    // bar01, bar02, the billiard room and the kitchen corridor.
    rooms: [
        box(-1782.6, -1772.7, 5.5, 11.2, -6.5, -2.0),
        box(-1782.6, -1777.9, 11.1, 15.6, -6.5, -2.0),
        box(-1788.3, -1777.1, 15.7, 22.3, -6.5, -2.0),
        box(-1774.6, -1772.6, 11.2, 17.8, -6.5, -2.0),
    ],
    // Customers stand between the west stools (@Rbstol01-02, z 8.75), out of
    // reach of the Salieri's Back Window counter at the front door
    // (server/shops.ts, 3.2 m around "Salieri_save").
    counter: { position: { x: -1776.8, y: -5.5, z: 8.5 }, reach: 1.0 },
    // The label hangs over the west shop window, clear of the Back Window's
    // own over the front door.
    door: { x: -1778.6, y: -3.0, z: 4.9 },
    wake: { position: { x: -1776.3, y: -5.5, z: 3.6 }, heading: Math.PI },
    bartender: {
        model: 'Luigi.i3d',
        position: { x: -1776.1, y: -5.44, z: 10.26 },
        heading: -3.066,
        routine: BARMAN,
    },
    patrons: [
        {
            model: 'SalMan01K.i3d',
            position: { x: -1776.48, y: -5.51, z: 6.63 },
            heading: -1.565,
            routine: [['Mise zidle01st.i3d', 0]],
        },
        {
            model: 'Gangster01.i3d',
            position: { x: -1778.32, y: -5.46, z: 6.67 },
            heading: 1.564,
            routine: [['ZidleSed03.i3d', 0]],
        },
        {
            model: 'Enemy06.i3d',
            position: { x: -1779.78, y: -5.54, z: 6.8 },
            heading: -1.527,
            routine: [['ZidleSed04.i3d', 0]],
        },
        {
            model: 'Czena04.i3d',
            position: { x: -1778.45, y: -5.5, z: 9.6 },
            heading: Math.PI / 2,
            routine: GLASS,
            glass: true,
        },
        {
            model: 'sailor03.i3d',
            position: { x: -1778.47, y: -5.56, z: 19.44 },
            heading: -1.578,
            routine: [['GestZamStativ.i3d', 0]],
        },
    ],
};

const MORELLO: Bar = {
    id: 'morello',
    name: "Morello's Bar",
    tagline: 'Wrong side of the street',
    world: MAIN_WORLD,
    rooms: [box(-415.7, -406.0, 451.6, 461.2, 16.0, 20.5)],
    // The four stools in front of the counter (z 458.1).
    counter: { position: { x: -412.6, y: 17.35, z: 457.6 }, reach: 1.9 },
    door: null,
    wake: { position: { x: -411.5, y: 17.45, z: 449.6 }, heading: Math.PI },
    bartender: {
        model: 'civil33.i3d',
        position: { x: -410.61, y: 17.34, z: 460.1 },
        heading: 2.23,
        routine: BARMAN,
    },
    patrons: [
        {
            model: 'Enemy04K.i3d',
            position: { x: -413.63, y: 17.33, z: 455.39 },
            heading: 1.508,
            routine: [['GestikulaceNew01.i3d', 0]],
        },
        {
            model: 'Enemy12K.i3d',
            position: { x: -412.48, y: 17.39, z: 455.37 },
            heading: -1.577,
            routine: [['GestikulaceNew02.i3d', 0]],
        },
        {
            model: 'Enemy08K.i3d',
            position: { x: -409.16, y: 17.46, z: 458.67 },
            heading: -0.83,
            routine: GLASS,
            glass: true,
        },
        {
            model: 'Enemy10K.i3d',
            position: { x: -409.86, y: 17.37, z: 454.92 },
            heading: 3.028,
            routine: [['KoureniAutoStativ.i3d', 0]],
        },
    ],
};

/** The interior (lib/maps.ts) whose arrow leads into the bar, for the bars that have one. */
export function interiorId(bar: Bar): string {
    return `b:${bar.id === 'backroom' ? 'salieri-bar' : bar.id}`;
}

function backRoom(): Bar {
    const [counterEnd, talkA, talkB, seatA, seatB] = barRoom.patrons();

    return {
        id: 'backroom',
        name: "Salieri's Bar",
        tagline: 'The back room. Knock twice',
        world: barRoom.WORLD,
        built: true,
        rooms: null,
        counter: barRoom.order(),
        door: null,
        // Out of Salieri's door on stock Lost Heaven.
        wake:
            maps.MAPS.freeride.interiors?.find((entry) => entry.id === 'b:salieri-bar')?.outside ??
            null,
        bartender: { model: 'Luigi.i3d', ...barRoom.bartender(), routine: BARMAN },
        patrons: [
            { model: 'Czena04.i3d', ...counterEnd, routine: GLASS, glass: true },
            { model: 'Enemy04K.i3d', ...talkA, routine: [['GestikulaceNew01.i3d', 0]] },
            { model: 'Enemy12K.i3d', ...talkB, routine: [['GestikulaceNew02.i3d', 0]] },
            { model: 'SalMan01K.i3d', ...seatA, routine: [['Mise zidle01st.i3d', 0]] },
            { model: 'Gangster01.i3d', ...seatB, routine: [['ZidleSed03.i3d', 0]] },
        ],
    };
}

export function barsHere(): Bar[] {
    const map = maps.current();

    if (map?.id === 'freeride_extended') {
        return [SALIERI, MORELLO];
    }

    if (map?.id === 'freeride') {
        return [backRoom()];
    }

    return [];
}
