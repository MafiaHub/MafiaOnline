// Every home on every map (data/houses.js): the building, what it is called
// and costs, its door and, at a flat or a house, its parking spot at the kerb.
import HOUSES from '../data/houses.js';
import * as rooms from '../lib/house_rooms';
import { nearest, perMap } from '../lib/spots';
import type { HouseRow, HouseTier } from '../data/types';
import type { MapId } from '../lib/maps';
import type { Placement, Point } from '../lib/util';

const FIRST_WORLD = 0x52000000;
const DOOR_REACH = 1.8;
const UNKNOWN_STYLE_PRICE = 5000;

/** What the arrow over a door for sale calls the kind of home. */
export const KIND_WORD: Readonly<Record<HouseTier, string>> = {
    poor: 'ROOM',
    middle: 'FLAT',
    rich: 'VILLA',
};

// Building styles: what the street sees, what it costs. The price follows
// the building and the district it stands in: a tenement room is a couple of
// hours of jobs, a flat a few evenings, a villa a long-term goal.
// prettier-ignore
const STYLES: Readonly<Record<string, readonly [style: string, price: number]>> = {
    '4build142': ['Tenement room', 3800], '4build163': ['Walk-up room', 3500], '3rad03': ['Tenement room', 4200],
    '3rad02': ['Room over the shops', 4000], '9dum1': ['Room over the shops', 3600], '9dum2': ['Brick tenement room', 4400],
    '3rad07': ['Brick walk-up room', 4600], '%radovk21': ['Row-house room', 4800], '%zzdum2': ['Clapboard house room', 3200],
    '%zzdum3': ['Clapboard house room', 3200], '9dumch1': ['Clapboard cottage room', 3400], '9dumch3': ['Clapboard cottage room', 3400],
    '3rad04': ['Stone walk-up room', 4600], '4build29': ['Corner-house room', 4000], '9dum8r': ['Red-brick tenement room', 4400],
    '%dum2': ['Clapboard room', 3000], 'chajda03': ['Log cabin', 2800],
    '%cinzak2': ['Apartment in the blue house', 14500], '%cinzak3': ['Apartment in the blue house', 14500],
    '%radovk10': ['Apartment over the arcade', 16500], '%radovk5': ['Apartment in the grand block', 18000],
    '3rad08': ['Brownstone apartment', 17000], '3rad06': ['Red-brick apartment', 15000], '3rad05': ['Rose-stone apartment', 14000],
    '3rad10': ['Rose-stone apartment', 14000], '9dum6r': ['Apartment in the yellow block', 13500], '4build56': ['Brick apartment', 15500],
    '4build46': ['Apartment in the stately block', 19000], '4build40': ['Brick apartment', 15000], '4build38z': ['Corner apartment', 15500],
    '%radovk30': ['Townhouse apartment', 16000], '%obldum1': ['White stucco apartment', 13000], '4build76': ['Town-house apartment', 14000],
    '#rad042': ['Brick family house', 38000], '0viil': ['Stone townhouse', 62000], '0vila': ['Victorian villa', 75000],
    'vilap': ['Hilltop mansion', 95000], '9vila2': ['Brick mansion', 90000],
};

type HouseList = keyof typeof HOUSES;

const LISTS: readonly HouseList[] = ['city', 'extended', 'krajina'];

const MAP_LISTS: Readonly<Record<MapId, readonly HouseList[]>> = {
    freeride: ['city'],
    freeride_extended: ['city', 'extended'],
    freekrajina: ['krajina'],
};

/** A home that can be bought, whoever owns it. */
export interface House {
    id: string;
    tier: HouseTier;
    model: string;
    style: string;
    price: number;
    list: HouseList;
    /** Its number within its list, for its title. */
    number: number;
    /** The virtual world its rooms stand in. */
    world: number;
    door: Point;
    heading: number;
    /** Where people come out of it. */
    out: Placement;
    parking: Placement | null;
}

function houseOf(row: HouseRow, list: HouseList, number: number, world: number): House {
    const [id, tier, model, x, y, z, heading] = row;
    const [style, price] = STYLES[model] ?? [rooms.KINDS[tier].title, UNKNOWN_STYLE_PRICE];
    const parking: Placement | null =
        row.length === 11
            ? { position: { x: row[7], y: row[8], z: row[9] }, heading: row[10] }
            : null;

    return Object.freeze({
        id,
        tier,
        model,
        style,
        price,
        list,
        number,
        world,
        door: { x, y, z },
        heading,
        out: { position: { x, y: y + 0.05, z }, heading },
        parking,
    });
}

// Every house of every map, by id; world ids follow the file's order.
function catalogue(): Map<string, House> {
    const all = new Map<string, House>();
    let index = 0;

    for (const list of LISTS) {
        let number = 0;

        for (const row of HOUSES[list] ?? []) {
            all.set(row[0], houseOf(row, list, ++number, FIRST_WORLD + ++index));
        }
    }

    return all;
}

export const ALL: ReadonlyMap<string, House> = catalogue();

/** The houses of the current map. */
export const here = perMap<readonly House[]>(
    (map) => [...ALL.values()].filter((house) => MAP_LISTS[map.id].includes(house.list)),
    [],
);

export function title(house: House): string {
    return `${house.style} No. ${house.number}`;
}

/** The street door of this map's homes the position is at, or null. */
export function doorAt(position: Point): House | null {
    return nearest(here(), position, DOOR_REACH, (house) => house.door);
}
