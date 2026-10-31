// The insides of the homes Free Ride sells (server/housing/). Like the
// back-room bar (lib/bar_room.ts) and the county jail (lib/prison.ts) they
// are built from stock a2.dta models (MapObject) 150 m under the empty
// fields north of the city, more than 590 m from any road on every map.
// One copy of each room serves every home of its kind: the shell and its
// furniture stand in every virtual world, each home is its own world, and
// only a home's own decorations (lib/house_decor.ts) are created in it.
//
// Three kinds, matched to the buildings outside:
//  room    a tenement room (poor homes): 6 x 4.5 m, 2.5 m high, built from
//          planes like the jail: xzed.4ds wall boxes faced with stained
//          plaster (plotcihly4, ^PLOT03), weathered boards underfoot
//          (plotdrev, PRKNA01), a bare concrete ceiling (plotcihly3), a
//          steel door (plotdrev7), an iron stove, a cot, a plank table
//          (xstul10), a wash basin and a bulb on a cord.
//  flat    an apartment (middle homes): models/piica.4ds, a stock house
//          whose room (local x -0.9..6.04, z -4.0..3.85, floor y 5.19,
//          ceiling 8.69) is papered in striped wallpaper over a wood dado
//          (0FREE01) on a herringbone parquet (DEVKA3), with one panelled
//          door (0V1DVE, x 0.48..1.76 on its south wall). It is furnished
//          as a living room and a bedroom with the stock xgauc/xkreslo/
//          xskrin/xpostel/xstul pieces.
//  villa   three rooms (rich homes): the salon is models/myhouse.4ds, the
//          furnished version of the same house (room x -0.16..6.98,
//          z -4.01..4.39, floor 5.17, ceiling 8.35: leather sofa and
//          armchairs, wardrobes, three curtained windows, doors on its
//          south (x 0.58..1.68), north (same x) and west (z -0.33..0.88)
//          walls); the dining room and the bedroom are two more piica rooms,
//          dressed in the villa set of the story missions (9stulbig,
//          9zidle2, 9skrn3b, 9postel2, 9lustr*, 9kober*, 9krb, 9palma ...).
//          The salon's north door leads to the dining room and its west
//          door to the bedroom (an arrow at each, press G); a fire burns
//          in the dining-room fireplace while someone is home.
// Mafia has no script lights; lamps are the stock "on" models, whose glass
// is drawn lit.
//
// Coordinates: each room's local frame is the model's own (the plane-built
// room's is its floor centre), placed unturned at `origin`, so local + origin
// is world. Furniture is placed by its bounding box (BOXES, measured from
// the stock 4DS LOD0 meshes with their frame transforms): `put` stands a
// model's box centre on a floor point, turned to face a heading. A model
// faces local -z unless FRONTS says otherwise; Euler yaw h turns local +z to
// (sin h, 0, cos h) (glm::angleAxis about up, as client MapObjectService
import * as log from './log';
import {
    MapStructure,
    QUARTER,
    available,
    offset,
    relative,
    tryCreate,
    type ObjectOptions,
    type Piece,
} from './map_objects';
import type { HouseTier } from '../data/types';
import type { Placement, Point } from './util';

export { QUARTER, available };

const ALL_WORLDS = -1;

/** A model's bounding box [minX, maxX, minY, maxY, minZ, maxZ]. */
export type Box = readonly [number, number, number, number, number, number];

// [minX, maxX, minY, maxY, minZ, maxZ] in model space.
// prettier-ignore
export const BOXES = {
    'xzed': [-3, 3, 0, 2.5, -0.12, 0.12],
    'plotcihly3': [-6, 6, -1.35, 1.35, 0, 0], 'plotcihly4': [-6, 6, -1.35, 1.35, 0, 0], 'plotdrev': [-6, 6, -1.35, 1.35, 0, 0],
    'plotdrev7': [-1.4, 0, -1.16, 1.24, 0, 0.1], '2zarc': [-0.02, 0.02, 0.09, 0.41, -0.02, 0.02],
    'piica': [-15.08, 7.4, 3.52, 16.61, -7.5, 5.26], 'myhouse': [-29.58, 7.4, 3.51, 16.61, -7.5, 13.65],
    'xpostel 1': [-1.46, 0.54, -0.7, 0.66, -0.4, 2.02], 'xpostel 2': [-0.64, 0.66, -0.41, 0.7, -0.38, 2.04],
    'xtaska 1': [-0.38, 0.38, 0, 0.6, -0.1, 0.1], 'xstul10': [-0.5, 0.5, -0.8, 0.05, -0.75, 0.75],
    'xstul01': [-0.74, 0.74, -0.35, 0.35, -0.51, 0.51], 'xstul05': [-1.25, 1.25, -0.85, 0.05, -0.55, 0.55],
    'xzidle 01': [-0.22, 0.3, -0.52, 0.58, -0.25, 0.25], 'xflaska 1': [-0.05, 0.04, 0, 0.32, -0.04, 0.04],
    'xpopelnik': [-0.04, 0.08, 0, 0.04, -0.07, 0.05], '9KAMN': [-0.54, 0.25, -0.6, 1.71, -0.25, 0.24],
    'xkybl 1': [-0.19, 0.19, 0, 0.4, -0.18, 0.18], 'xumyvadlo': [0, 0.5, -1.04, 0.03, -0.34, 0.36],
    'xskrin 05': [-0.63, 0.67, -0.96, 0.03, -0.3, 0.19], 'xradio 1': [-0.09, 0.09, 0, 0.45, -0.14, 0.14],
    'xobraz13': [-0.25, 0.25, 0, 0.35, -0.01, 0.01], 'xrohozka': [-0.21, 0.59, 0, 0.02, -0.41, 0.14],
    'xkoberec': [-1.4, 2.5, 0, 0, -1.4, 2.5], 'xgauc 01': [-1.2, 1.19, 0, 1.02, -1.06, -0.06],
    'xkreslo 2': [-0.37, 0.37, -0.36, 1.02, -0.24, 0.5], 'xskrin 06': [-0.55, 0.55, -0.05, 0.92, -0.4, 0.35],
    'xkvet02': [-0.49, 0.49, -0.41, 0.41, -0.49, 0.49], 'xkvet01': [-0.2, 0.2, 0, 0.65, -0.23, 0.23],
    'xvesak': [-0.39, 0.25, -0.01, 1.8, -0.4, 0.24], 'xskrin 21': [-0.63, 0.63, -0.08, 2, -0.36, 0.32],
    'xskrin 12': [-0.37, 0.37, 0, 0.72, -0.26, 0.24], '9lampic2': [-0.21, 0.21, 0, 0.73, -0.18, 0.18],
    'xobraz01': [-0.23, 0.23, -0.23, 0.23, -0.02, 0], 'xobraz02': [-0.23, 0.23, -0.22, 0.22, -0.02, 0],
    'xobraz05': [-0.23, 0.23, -0.22, 0.22, -0.02, 0], 'xsvetlo 10 on': [-0.27, 0.27, -0.24, 0, -0.27, 0.27],
    '9lampa1': [-0.2, 0.22, -0.72, 0.92, -0.24, 0.25], 'xsporak': [-0.42, 0.42, 0, 1.93, -0.4, 0.45],
    '9lustr2': [-0.65, 0.63, -0.49, 0.76, -0.64, 0.64], '9kober1': [-2.24, 2.24, -0.02, -0.02, -1.44, 1.44],
    'xpiano': [-0.95, 0.91, -0.04, 1.59, -0.31, 0.33], 'fikus': [-0.53, 0.49, -0.16, 1.82, -0.46, 0.63],
    '9hodin1': [-0.25, 0.27, -1.05, 1.06, -0.21, 0.18], '9obr8': [-0.51, 0.52, -0.54, 0.54, -0.03, 0.02],
    '9stulbig': [-3.14, 3.12, -0.41, 0.53, -1.04, 1.13], '9zidle2': [-0.39, 0.27, -0.53, 1.03, -0.28, 0.44],
    '9skrn3b': [-0.68, 0.68, -1.25, 1.01, -0.35, 0.34], '9skrinhod': [-0.34, 0.37, -0.32, 1.2, -0.2, 0.23],
    '9krb': [-0.96, 0.98, -0.67, 0.97, -0.14, 0.56], '9stolov': [-0.98, 0.97, 0.01, 0.81, -0.39, 0.39],
    '9vaza2': [-0.15, 0.15, -0.08, 0.38, -0.14, 0.14], '9palma': [-0.93, 0.79, -0.16, 1.6, -0.82, 0.89],
    '9lustr1': [-0.37, 0.3, -0.11, 0.67, -0.33, 0.33], '9kober3': [-5.1, 5.1, 0, 0, -2.07, 2.07],
    '9obr3': [-0.29, 0.29, -0.33, 0.33, -0.03, 0.02], '9srnec': [-0.8, 0.78, -0.32, 0.92, -0.34, 0.26],
    '9postel2': [-1.25, 1.22, 0, 1.66, -1.53, 1.79], '9stolek2': [-0.42, 0.43, -0.52, 0.36, -0.41, 0.45],
    '9skrink5': [-0.51, 0.52, -0.43, 2, -0.26, 0.25], '9skrink1': [-0.62, 0.62, 0, 1.07, -0.31, 0.29],
    '9zrcadlo': [-0.28, 0.28, -0.28, 0.28, 0.02, 0.05], '9kresl1': [-0.75, 0.75, -0.15, 1.07, -0.51, 0.66],
    '9otoman': [-1.5, 1.51, -0.15, 1.05, -0.51, 0.68], '9stolkul': [-0.37, 0.38, -0.38, 0.5, -0.37, 0.38],
    '9kober4': [-2.24, 2.24, -0.02, -0.02, -1.44, 1.44], '9lustr4 on': [-0.48, 0.48, -0.02, 0.84, -0.48, 0.48],
    '9obr5': [-0.45, 0.43, -0.22, 0.38, -0.02, 0], '9truhla': [-0.54, 0.53, -0.3, 0.29, -0.31, 0.29],
    '9vesak3': [-0.18, 0.32, -1.33, 1.05, -0.3, 0.27],
} satisfies Readonly<Record<string, Box>>;

export type BoxedModel = keyof typeof BOXES;

// Local heading of a model's front where it is not -z: the kitchen chair's
// back is its +x side (as in lib/bar_room.ts), the wall basin hangs from its
// -x side, and the dining chair xzidle 02 sits facing +z.
const FRONTS: Readonly<Partial<Record<string, number>>> = {
    'xzidle 01': -QUARTER,
    xumyvadlo: QUARTER,
    'xzidle 02': 0,
};
// Lamps, rugs and pictures do not collide: nobody trips on a rug's edge or
// catches their head on a chandelier.
const PASSABLE: ReadonlySet<string> = new Set([
    '2zarc',
    'xrohozka',
    'xkoberec',
    '9kober1',
    '9kober3',
    '9kober4',
    'xsvetlo 10 on',
    '9lustr1',
    '9lustr2',
    '9lustr4 on',
    'xobraz01',
    'xobraz02',
    'xobraz13',
    'xobraz05',
    '9obr3',
    '9obr5',
    '9obr8',
    '9zrcadlo',
    '9srnec',
]);

// Which way a piece faces.
const N = 0;
const E = QUARTER;
const S = Math.PI;
const W = -QUARTER;

export interface PutOptions {
    /** y is where the top goes (a ceiling lamp), not the bottom. */
    top?: boolean;
    scale?: Point | null;
}

/**
 * Where to put a model's origin so its box centre stands at (x, z) with its
 * bottom at y (or, with {top}, its top at y), facing `facing`.
 */
export function put(
    model: BoxedModel,
    x: number,
    y: number,
    z: number,
    facing = 0,
    { top = false, scale = null }: PutOptions = {},
): Piece {
    const [x0, x1, y0, y1, z0, z1] = BOXES[model];
    const sx = scale?.x ?? 1;
    const sy = scale?.y ?? 1;
    const sz = scale?.z ?? 1;
    const yaw = facing - (FRONTS[model] ?? Math.PI);
    const cx = ((x0 + x1) / 2) * sx;
    const cz = ((z0 + z1) / 2) * sz;
    // The box centre turned by the yaw: +z goes to (sin, cos), +x to (cos, -sin).
    const wx = cx * Math.cos(yaw) + cz * Math.sin(yaw);
    const wz = -cx * Math.sin(yaw) + cz * Math.cos(yaw);
    const oy = top ? y - y1 * sy : y - y0 * sy;

    return {
        model,
        position: { x: x - wx, y: oy, z: z - wz },
        rotation: { x: 0, y: yaw, z: 0 },
        scale,
        passable: PASSABLE.has(model),
    };
}

// A raw piece, placed by its origin.
function raw(model: string, position: Point, rotation: Point, scale: Point | null = null): Piece {
    return { model, position, rotation, scale, passable: PASSABLE.has(model) };
}

// ---- The tenement room ----------------------------------------------------
const ROOM_X = 3;
const ROOM_Z = 2.25;
const ROOM_H = 2.5;

function roomPieces(): Piece[] {
    const list: Piece[] = [];

    for (const z of [-ROOM_Z - 0.12, ROOM_Z + 0.12]) {
        list.push(raw('xzed', { x: 0, y: 0, z }, { x: 0, y: 0, z: 0 }));
    }

    for (const x of [-ROOM_X - 0.12, ROOM_X + 0.12]) {
        list.push(
            raw(
                'xzed',
                { x, y: 0, z: 0 },
                { x: 0, y: QUARTER, z: 0 },
                { x: (2 * ROOM_Z + 0.24) / 6, y: 1, z: 1 },
            ),
        );
    }

    // Stained plaster 5 cm in front of every wall; it runs 0.2 m on above the ceiling.
    for (const z of [-ROOM_Z + 0.05, ROOM_Z - 0.05]) {
        list.push(
            raw('plotcihly4', { x: 0, y: 1.35, z }, { x: 0, y: 0, z: 0 }, { x: 0.5, y: 1, z: 1 }),
        );
    }

    for (const x of [-ROOM_X + 0.05, ROOM_X - 0.05]) {
        list.push(
            raw(
                'plotcihly4',
                { x, y: 1.35, z: 0 },
                { x: 0, y: QUARTER, z: 0 },
                { x: (2 * ROOM_Z) / 12, y: 1, z: 1 },
            ),
        );
    }

    list.push(
        raw(
            'plotdrev',
            { x: 0, y: 0, z: 0 },
            { x: QUARTER, y: 0, z: 0 },
            { x: 0.5, y: (2 * ROOM_Z) / 2.7, z: 1 },
        ),
    );

    list.push(
        raw(
            'plotcihly3',
            { x: 0, y: ROOM_H, z: 0 },
            { x: QUARTER, y: 0, z: 0 },
            { x: 0.5, y: (2 * ROOM_Z) / 2.7, z: 1 },
        ),
    );

    // The steel door on the south wall, x -1.4..0.
    list.push(raw('plotdrev7', { x: 0, y: 1.16, z: -ROOM_Z + 0.06 }, { x: 0, y: 0, z: 0 }));
    list.push(put('xpostel 2', 2.3, 0, 0.95, S));
    list.push(put('xtaska 1', 2.35, 0, -0.75, N));
    list.push(put('xstul10', -1.5, 0, 1.4, S));
    list.push(put('xzidle 01', -0.72, 0, 1.4, W));
    list.push(put('xflaska 1', -1.75, 0.85, 1.75));
    list.push(put('xpopelnik', -1.3, 0.85, 1.1));
    list.push(put('9KAMN', -2.55, 0, 1.95, S));
    list.push(put('xkybl 1', -2.6, 0, 1.15));
    list.push(put('xumyvadlo', -ROOM_X + 0.05 + 0.25, 0, -0.6, E));
    list.push(put('xskrin 05', 1.6, 0, -ROOM_Z + 0.05 + 0.25, N));
    list.push(put('xradio 1', 1.3, 0.99, -ROOM_Z + 0.3, N));
    list.push(put('xobraz13', -1.0, 1.4, ROOM_Z - 0.06, S));
    list.push(put('xrohozka', -0.7, 0.005, -1.75));
    list.push(put('2zarc', 0, ROOM_H, 0.2, N, { top: true }));

    return list;
}

// ---- The apartment (a piica room) ------------------------------------------
const PIICA = Object.freeze({
    x0: -0.9,
    x1: 6.04,
    z0: -4.0,
    z1: 3.85,
    floor: 5.19,
    ceiling: 8.69,
    door: { x: 1.12, z: -3.95 },
});

function flatPieces(): Piece[] {
    const f = PIICA.floor;

    return [
        raw('piica', { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 0 }),
        put('xkoberec', 3.6, f + 0.01, -1.6),
        put('xgauc 01', 3.6, f, -0.35, S),
        put('xkreslo 2', 2.7, f, -2.6, N),
        put('xkreslo 2', 4.5, f, -2.6, N),
        put('xstul01', 3.6, f, -1.6),
        put('9lampa1', 5.2, f, -0.4, S),
        put('xskrin 06', 5.4, f, PIICA.z0 + 0.42, N),
        put('xradio 1', 5.4, f + 0.97, PIICA.z0 + 0.42, N),
        put('xkvet02', PIICA.x1 - 0.55, f, -2.1),
        put('xobraz05', 3.6, f + 1.75, PIICA.z0 + 0.03, N),
        put('xsporak', -0.45, f, -2.0, E),
        put('xumyvadlo', PIICA.x0 + 0.25, f, -1.0, E),
        put('xvesak', -0.4, f, -3.55, N),
        put('xstul05', 0.7, f, 0.9),
        put('xzidle 01', 0.15, f, 1.85, S),
        put('xzidle 01', 1.25, f, 1.85, S),
        put('xzidle 01', 0.15, f, -0.05, N),
        put('xzidle 01', 1.25, f, -0.05, N),
        put('xobraz02', PIICA.x0 + 0.03, f + 1.6, 0.9, E),
        put('xkvet01', 5.75, f, 0.55),
        put('xskrin 21', 0.0, f, PIICA.z1 - 0.37, S),
        put('xpostel 1', 4.4, f, 2.6, S),
        put('xskrin 12', 2.95, f, PIICA.z1 - 0.3, S),
        put('9lampic2', 2.95, f + 0.72, PIICA.z1 - 0.3, S),
        put('xobraz01', 4.4, f + 1.75, PIICA.z1 - 0.03, S),
        put('xsvetlo 10 on', 3.4, PIICA.ceiling, -1.4, N, { top: true }),
        put('xsvetlo 10 on', 2.6, PIICA.ceiling, 2.0, N, { top: true }),
    ];
}

// ---- The villa -------------------------------------------------------------
const SALON = Object.freeze({
    x0: -0.16,
    x1: 6.98,
    z0: -4.01,
    z1: 4.39,
    floor: 5.17,
    ceiling: 8.35,
    south: { x: 1.13, z: -3.96 },
    north: { x: 1.13, z: 4.32 },
    west: { x: -0.16, z: 0.28 },
});

function salonPieces(): Piece[] {
    const f = SALON.floor;

    return [
        raw('myhouse', { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 0 }),
        put('9kober1', 4.0, f + 0.01, -0.3),
        put('9lustr2', 3.4, SALON.ceiling, 0.2, 0, { top: true }),
        put('xpiano', SALON.x0 + 0.34, f, -2.0, E),
        put('fikus', 0.45, f, 2.2),
        put('9hodin1', 3.25, f, SALON.z1 - 0.21, S),
        put('9lampa1', 3.0, f, -2.6, 0),
    ];
}

function diningPieces(): Piece[] {
    const f = PIICA.floor;
    const list: Piece[] = [
        raw('piica', { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 0 }),
        put('9kober3', 3.1, f + 0.01, 0.6, E, { scale: { x: 0.6, y: 1, z: 1 } }),
        put('9stulbig', 3.1, f, 0.6, E, { scale: { x: 0.75, y: 1, z: 1 } }),
        put('9skrn3b', 1.0, f, PIICA.z1 - 0.36, S),
        put('9skrinhod', 5.2, f, PIICA.z1 - 0.23, S),
        put('9obr3', 3.1, f + 1.9, PIICA.z1 - 0.03, S),
        put('9krb', PIICA.x0 + 0.36, f, 0.6, E),
        put('9srnec', PIICA.x0 + 0.32, f + 2.25, 0.6, E),
        put('9stolov', PIICA.x1 - 0.4, f, 0.0, W),
        put('9vaza2', PIICA.x1 - 0.4, f + 0.81, -0.55),
        put('9vaza2', PIICA.x1 - 0.4, f + 0.81, 0.55),
        put('9obr8', PIICA.x1 - 0.03, f + 1.85, 0.0, W),
        put('9palma', 5.0, f, -3.1),
        put('fikus', -0.35, f, -3.35),
        put('9lustr1', 3.1, PIICA.ceiling, -0.3, N, { top: true }),
        put('9lustr1', 3.1, PIICA.ceiling, 1.7, N, { top: true }),
    ];

    for (const z of [-1.0, 0.2, 1.4, 2.6]) {
        list.push(put('9zidle2', 1.6, f, z, E));
        list.push(put('9zidle2', 4.6, f, z, W));
    }

    return list;
}

function bedroomPieces(): Piece[] {
    const f = PIICA.floor;

    return [
        raw('piica', { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 0 }),
        put('9kober4', 3.5, f + 0.01, -1.2),
        put('9postel2', 3.5, f, 2.1, S),
        put('9stolek2', 1.75, f, PIICA.z1 - 0.45, S),
        put('9stolek2', 5.25, f, PIICA.z1 - 0.45, S),
        put('9lampic2', 1.75, f + 0.88, PIICA.z1 - 0.45, S),
        put('9lampic2', 5.25, f + 0.88, PIICA.z1 - 0.45, S),
        put('9obr5', 3.5, f + 2.1, PIICA.z1 - 0.03, S),
        put('9truhla', 3.5, f, 0.05, S),
        put('9skrink5', PIICA.x0 + 0.26, f, 1.8, E),
        put('9skrink1', PIICA.x0 + 0.31, f, -0.6, E),
        put('9zrcadlo', PIICA.x0 + 0.03, f + 1.55, -0.6, E),
        put('9kresl1', PIICA.x1 - 0.6, f, -1.8, W),
        put('9stolkul', 4.3, f, -1.8),
        put('9otoman', 3.6, f, PIICA.z0 + 0.6, N),
        put('9vesak3', -0.45, f, -3.55, N),
        put('9lustr4 on', 2.57, PIICA.ceiling, 0.0, N, { top: true }),
    ];
}

export type RoomKey = 'room' | 'flat' | 'salon' | 'dining' | 'bedroom';

const ROOM_KEYS: readonly RoomKey[] = ['room', 'flat', 'salon', 'dining', 'bedroom'];

/** The box (local) a room's floor spans. */
export interface Bounds {
    x0: number;
    x1: number;
    z0: number;
    z1: number;
    floor: number;
    ceiling: number;
}

export interface RoomDoor {
    at: Point;
    /** The room it leads to; "street" leaves the home. */
    to: RoomKey | 'street';
    label: string;
}

export interface Room {
    origin: Point;
    pieces: () => Piece[];
    /** Where a decoration may move. */
    bounds: Bounds;
    /** The wall faces, where a picture hangs. */
    walls: Omit<Bounds, 'floor' | 'ceiling'>;
    /** Where people arrive from the street. */
    arrive: Placement;
    doors: RoomDoor[];
    /** Where people arrive coming back from another room. */
    returns?: Partial<Record<RoomKey, Placement>>;
    /** The hearth a fire burns in while someone is home. */
    fire?: Point;
}

// Every room: where its local origin stands, its pieces, the box a
// decoration may move in (local), where people arrive, and its doors. A door
// is {at, to: room key | 'street', label}; 'street' leaves the home.
export const ROOMS: Readonly<Record<RoomKey, Room>> = {
    room: {
        origin: { x: -560, y: -150, z: 1500 },
        pieces: roomPieces,
        bounds: {
            x0: -ROOM_X + 0.15,
            x1: ROOM_X - 0.15,
            z0: -ROOM_Z + 0.15,
            z1: ROOM_Z - 0.15,
            floor: 0,
            ceiling: ROOM_H,
        },
        walls: { x0: -ROOM_X + 0.05, x1: ROOM_X - 0.05, z0: -ROOM_Z + 0.05, z1: ROOM_Z - 0.05 },
        arrive: { position: { x: -0.7, y: 0.05, z: -1.4 }, heading: 0 },
        doors: [{ at: { x: -0.7, y: 0, z: -1.85 }, to: 'street', label: 'EXIT' }],
    },
    flat: {
        origin: { x: -642.57, y: -155.19, z: 1500.08 },
        pieces: flatPieces,
        bounds: {
            x0: PIICA.x0 + 0.15,
            x1: PIICA.x1 - 0.15,
            z0: PIICA.z0 + 0.15,
            z1: PIICA.z1 - 0.15,
            floor: PIICA.floor,
            ceiling: PIICA.ceiling,
        },
        walls: { x0: PIICA.x0, x1: PIICA.x1, z0: PIICA.z0, z1: PIICA.z1 },
        arrive: { position: { x: PIICA.door.x, y: PIICA.floor + 0.05, z: -3.0 }, heading: 0 },
        doors: [{ at: { x: PIICA.door.x, y: PIICA.floor, z: -3.5 }, to: 'street', label: 'EXIT' }],
    },
    salon: {
        origin: { x: -723.41, y: -155.17, z: 1499.81 },
        pieces: salonPieces,
        bounds: {
            x0: SALON.x0 + 0.15,
            x1: SALON.x1 - 0.25,
            z0: SALON.z0 + 0.15,
            z1: SALON.z1 - 0.15,
            floor: SALON.floor,
            ceiling: SALON.ceiling,
        },
        // The east wall's curtains stand 0.18 m into the room.
        walls: { x0: SALON.x0, x1: SALON.x1 - 0.18, z0: SALON.z0, z1: SALON.z1 },
        arrive: { position: { x: SALON.south.x, y: SALON.floor + 0.05, z: -3.2 }, heading: 0 },
        doors: [
            { at: { x: SALON.south.x, y: SALON.floor, z: -3.5 }, to: 'street', label: 'EXIT' },
            {
                at: { x: SALON.north.x, y: SALON.floor, z: 3.75 },
                to: 'dining',
                label: 'DINING ROOM',
            },
            { at: { x: 0.35, y: SALON.floor, z: SALON.west.z }, to: 'bedroom', label: 'BEDROOM' },
        ],
        // Coming back from the other rooms.
        returns: {
            dining: {
                position: { x: SALON.north.x, y: SALON.floor + 0.05, z: 3.3 },
                heading: Math.PI,
            },
            bedroom: {
                position: { x: 0.75, y: SALON.floor + 0.05, z: SALON.west.z },
                heading: QUARTER,
            },
        },
    },
    dining: {
        origin: { x: -802.57, y: -155.19, z: 1500.08 },
        pieces: diningPieces,
        bounds: {
            x0: PIICA.x0 + 0.15,
            x1: PIICA.x1 - 0.15,
            z0: PIICA.z0 + 0.15,
            z1: PIICA.z1 - 0.15,
            floor: PIICA.floor,
            ceiling: PIICA.ceiling,
        },
        walls: { x0: PIICA.x0, x1: PIICA.x1, z0: PIICA.z0, z1: PIICA.z1 },
        arrive: { position: { x: PIICA.door.x, y: PIICA.floor + 0.05, z: -3.0 }, heading: 0 },
        doors: [{ at: { x: PIICA.door.x, y: PIICA.floor, z: -3.5 }, to: 'salon', label: 'SALON' }],
        // The hearth of the 9krb fireplace on the west wall.
        fire: { x: PIICA.x0 + 0.25, y: PIICA.floor + 0.08, z: 0.6 },
    },
    bedroom: {
        origin: { x: -882.57, y: -155.19, z: 1500.08 },
        pieces: bedroomPieces,
        bounds: {
            x0: PIICA.x0 + 0.15,
            x1: PIICA.x1 - 0.15,
            z0: PIICA.z0 + 0.15,
            z1: PIICA.z1 - 0.15,
            floor: PIICA.floor,
            ceiling: PIICA.ceiling,
        },
        walls: { x0: PIICA.x0, x1: PIICA.x1, z0: PIICA.z0, z1: PIICA.z1 },
        arrive: { position: { x: PIICA.door.x, y: PIICA.floor + 0.05, z: -3.0 }, heading: 0 },
        doors: [{ at: { x: PIICA.door.x, y: PIICA.floor, z: -3.5 }, to: 'salon', label: 'SALON' }],
    },
};

/** A kind of home: its rooms and the one people enter. */
export interface HomeKind {
    key: string;
    title: string;
    rooms: RoomKey[];
    entry: RoomKey;
}

export const KINDS: Readonly<Record<HouseTier, HomeKind>> = {
    poor: { key: 'room', title: 'Tenement room', rooms: ['room'], entry: 'room' },
    middle: { key: 'flat', title: 'Apartment', rooms: ['flat'], entry: 'flat' },
    rich: { key: 'villa', title: 'Villa', rooms: ['salon', 'dining', 'bedroom'], entry: 'salon' },
};

export function toWorld(roomKey: RoomKey, local: Point): Point {
    return offset(ROOMS[roomKey].origin, local);
}

export function toLocal(roomKey: RoomKey, position: Point): Point {
    return relative(ROOMS[roomKey].origin, position);
}

/** The room a position is in, with a margin, or null. */
export function roomAt(position: Point, margin = 1.5): RoomKey | null {
    for (const key of ROOM_KEYS) {
        const local = toLocal(key, position);
        const { bounds } = ROOMS[key];

        if (
            local.x >= bounds.x0 - margin &&
            local.x <= bounds.x1 + margin &&
            local.z >= bounds.z0 - margin &&
            local.z <= bounds.z1 + margin &&
            local.y >= bounds.floor - 3 &&
            local.y <= bounds.ceiling + 3
        ) {
            return key;
        }
    }

    return null;
}

export function pose(roomKey: RoomKey, entry: Placement): Placement {
    return { position: toWorld(roomKey, entry.position), heading: entry.heading };
}

// ---- Building --------------------------------------------------------------

// MapObject.create takes a bare model file name of letters, digits, spaces,
// "_", "-" and "." only, and throws otherwise; one bad piece must never take
// the gamemode down, so it is logged and left out.
export const MODEL_NAME = /^[A-Za-z0-9_\-. ]{1,59}$/;

/** Places a model (its name without the extension); null when it is refused. */
export function create(
    model: string,
    position: Point,
    rotation: Point,
    options: ObjectOptions,
): MapObject | null {
    if (!MODEL_NAME.test(model)) {
        log.error(`Home piece ${model} has a model name MapObject cannot load; left out`);

        return null;
    }

    return tryCreate(`Home piece ${model}`, `${model}.i3d`, position, rotation, options);
}

function place(roomKey: RoomKey, piece: Piece): MapObject | null {
    const options: ObjectOptions = { virtualWorld: ALL_WORLDS, collision: !piece.passable };

    if (piece.scale) {
        options.scale = piece.scale;
    }

    return create(piece.model, toWorld(roomKey, piece.position), piece.rotation, options);
}

// A piece refused at the object limit is worth another try; the rooms
// stand without one that keeps being refused.
function lay(objects: MapObject[]): string | null {
    let refused = 0;

    for (const key of ROOM_KEYS) {
        let pieces: Piece[] = [];

        try {
            pieces = ROOMS[key].pieces();
        } catch (caught) {
            log.error(`Could not lay out the ${key}: ${log.describe(caught, true)}`);
        }

        for (const piece of pieces) {
            const object = place(key, piece);

            if (object) {
                objects.push(object);
            } else {
                refused++;
            }
        }
    }

    return refused && !objects.length ? `all ${refused} pieces were refused` : null;
}

const homes = new MapStructure('the homes', lay);

export function isBuilt(): boolean {
    return homes.isBuilt();
}

export function build(): void {
    homes.build();
}

/** A mission change has already removed the objects. */
export function forget(): void {
    homes.forget();
}

export function teardown(): void {
    homes.teardown();
}

/** How many objects the rooms take. */
export function pieceCount(): number {
    return ROOM_KEYS.reduce((sum, key) => sum + ROOMS[key].pieces().length, 0);
}
