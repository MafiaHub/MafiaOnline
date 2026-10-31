// The inside of Lost Heaven Hospital. The stock models/9medic.4ds is a shell
// 96 m wide with no rooms behind its doors, so, like the back-room bar
// (bar_room.ts) and the county jail (prison.ts), its floor is built from
// stock a2.dta models (MapObject) where nobody stumbles on it: 150 m under
// the empty fields north of the city, in its own virtual world. The spot,
// (-720, 1420), is 691 m from the nearest road of every map (the road graphs
// of lib/maps.ts) and 70 m south of the row of homes (house_rooms.ts, whose
// rooms reach z 1492); the jail (-400) and the bar (-460) are 260 m away and
// more. The block spans 42 by 24 m, local x -21..21 and z -11..13.
//
// Local coordinates: x runs along the corridor, z north across it, y up; the
// origin is on the floor where the entrance hall opens into the corridor.
// The rooms, all joined by open doorways:
//  entrance hall    x -7..7, z -11..0, 4.5 m high: the street door in the
//                   south wall, the reception desk on the east side with the
//                   records behind it, two rows of waiting chairs on the west
//                   side, a rug, palms by the 5 m opening into the corridor
//  corridor         x -21..21, z 0..3, 3.2 m high like every other room
//  consulting room  x -14..-7, z -7..0: the doctor's desk, an examination
//                   couch, a washbasin, his diploma and books
//  lift lobby       x 7..14, z -7..0: two closed lift doors, a bench
//  men's ward       x -21..-5, z 3..13: six beds along the north wall under
//                   frosted windows, four along the south wall either side
//                   of the door, curtain screens, the nurse's trolley
//  theatre          x -5..5, z 3..12: a bed under a hanging lamp, an
//                   instrument stand and trolley, glazed instrument
//                   cabinets, two scrub basins, a concrete floor
//  women's ward     x 5..21, z 3..13, the men's mirrored
//
// The models, their boxes [x0, x1, y0, y1, z0, z1] measured from the LOD 0
// meshes with their frame transforms (BOXES). A model faces local -z unless
// FRONTS says otherwise.
//  xzed          a 6 x 2.5 m wall box painted white over a green dado
//                (0XEX1), finished on both faces: every wall, stretched to
//                the room's height; a short piece over a doorway is its lintel
//  plotdrev      a 12 x 2.7 m double-sided plane of boards (PRKNA01): the
//                floors, tiled to fit
//  plotcihly3    the same plane in bare concrete: the ceilings, the
//                theatre's floor
//  plotdrev7     a white steel door with a notice: the street doors, the
//                lifts
//  9kober4       a 4.5 x 2.9 m rug, xvesak a coat stand, xkvet01 a potted
//                plant, fikus a palm in a tub, xobraz03 a portrait, xobraz09
//                a still life (both single-sided, facing -z), xobraz10 a
//                printed notice (facing +z), xhodiny 2 a wall clock (its
//                face to -z), 9tabul a board
//  9pstul2       a 2.7 m desk with a stone top 0.92 m up: the reception
//  telefon, xsvetlo 08 on (a lit banker's lamp), xkniha 1, 9notes and the
//                typewriter xstroj01: on desks
//  9skrinsk      a glazed case (records), xskrin 11 a white two-door
//                cabinet, xskrin 10 a white cabinet with glazed doors
//                (instruments), xskrin 08 a low white cupboard
//  xzidle 03     a white kitchen chair, the mesh of xzidle 01 that the bar's
//                drinkers sit on: back to its +x side, seat 0.55 m up
//  xpostel 2     an iron bed, 1.3 x 2.42 m, its head to +z; the mattress
//                top is 0.68 m up and runs from z -0.11 to 1.77
//  okno3         a frosted window (single-sided, faces +z)
//  zavesik       a curtain 1.96 m wide on z (single-sided, faces -x): each
//                screen is two, back to back, cut to 1.8 m
//  9vozik        a dressing trolley, xstul16 a desk with two pedestals
//                (0.78 m), umyvadlo a pedestal washbasin, 9lekarna a wall
//                medicine cabinet with a red cross (scaled 1.6), xstul19 a
//                white pedestal table (halved: the instrument stand),
//                xkybl 1 a bucket, xkoste 1 a broom, xlavicka a bench
//  xsvetlo 10 on the lit bowl lamp of the bar's ceiling, flush with the
//                ceiling; xsvetlo 09 on a lit bowl on chains (1.66 m), hung
//                in the hall and, shortened, over the theatre's bed
// Lamps, pictures, the rug, the curtains and the chairs people sit on do not
// collide.
import * as log from './log';
import {
    MapStructure,
    QUARTER,
    available,
    offset,
    optionsFor,
    relative,
    tryCreate,
    type Piece,
} from './map_objects';
import type { Placement, Point } from './util';

export { available };

const ORIGIN: Point = Object.freeze({ x: -720, y: -150, z: 1420 });

export const WORLD = 0x53000000;
const HEIGHT = 3.2;
const HALL_HEIGHT = 4.5;
// The doorways' and the hall opening's top.
const LINTEL = 2.3;
const OPENING_TOP = 3.0;
const WALL = 0.24;

type Box = readonly [number, number, number, number, number, number];

// [minX, maxX, minY, maxY, minZ, maxZ] in model space.
// prettier-ignore
export const BOXES = {
    'plotdrev7': [-1.4, 0, -1.16, 1.24, 0, 0.1],
    '9kober4': [-2.24, 2.24, -0.02, -0.02, -1.44, 1.44],
    'xvesak': [-0.39, 0.25, -0.01, 1.8, -0.4, 0.24],
    '9pstul2': [-1.3, 1.4, -0.61, 0.31, -0.52, 0.53],
    'telefon': [-0.11, 0.26, -0.02, 0.23, -0.17, 0.15],
    'xsvetlo 08 on': [-0.24, 0.26, -0.15, 0.54, -0.16, 0.21],
    'xkniha 1': [-0.15, 0.15, -0.02, 0.02, -0.18, 0.07],
    '9notes': [-0.06, 0.05, -0.01, 0.02, -0.1, 0.08],
    'xstroj01': [-0.06, 0.38, 0, 0.18, -0.18, 0.26],
    '9skrinsk': [-0.56, 0.54, -0.86, 1.16, -0.2, 0.17],
    'xskrin 11': [-0.45, 0.65, -0.07, 2.1, -0.24, 0.24],
    'xskrin 10': [-0.4, 0.4, -0.45, 0.45, -0.25, 0.25],
    'xskrin 08': [-0.4, 0.4, 0, 1, -0.49, 0.45],
    'xhodiny 2': [-0.25, 0.25, -0.8, 0, -0.04, 0],
    'xobraz03': [-0.23, 0.23, -0.22, 0.38, -0.02, 0],
    'xobraz09': [-0.23, 0.23, 0, 0.45, -0.01, 0.01],
    'xobraz10': [-0.04, 0.34, 0, 0.58, 0, 0.03],
    '9tabul': [-0.28, 0.26, -0.44, 0.41, -0.01, 0.02],
    'xzidle 03': [-0.22, 0.3, -0.52, 0.58, -0.25, 0.25],
    'xkvet01': [-0.2, 0.2, 0, 0.65, -0.23, 0.23],
    'fikus': [-0.53, 0.49, -0.16, 1.82, -0.46, 0.63],
    'xsvetlo 10 on': [-0.27, 0.27, -0.24, 0, -0.27, 0.27],
    'xsvetlo 09 on': [-0.3, 0.3, -0.37, 1.29, -0.3, 0.3],
    'xpostel 2': [-0.64, 0.66, -0.41, 0.7, -0.38, 2.04],
    'okno3': [-0.68, 0.64, -0.79, 0.66, 0, 0],
    '9vozik': [-0.66, 0.99, -0.28, 0.48, -0.4, 0.41],
    'xstul16': [-1, 1, -0.39, 0.39, -0.38, 0.38],
    'xstul19': [-0.75, 0.75, -0.44, 0.41, -0.75, 0.75],
    'umyvadlo': [-0.38, 0.38, -0.54, 0.54, -0.33, 0.33],
    '9lekarna': [-0.15, 0.17, -0.26, 0.24, -0.1, 0.11],
    'xkybl 1': [-0.19, 0.19, 0, 0.4, -0.18, 0.18],
    'xkoste 1': [-0.23, 0.24, -0.04, 1.82, -0.07, 0.06],
    'xlavicka': [-0.85, 0.85, 0, 0.82, -0.22, 0.39],
} satisfies Readonly<Record<string, Box>>;

type Model = keyof typeof BOXES;

// Local heading of a model's front where it is not -z.
const FRONTS: Readonly<Partial<Record<Model, number>>> = {
    'xzidle 03': -QUARTER,
    'xpostel 2': 0,
    okno3: 0,
    xobraz10: 0,
    // The bench's back is its -z edge; it seats towards +z.
    xlavicka: 0,
};

// Which way a piece faces.
const N = 0;
const E = QUARTER;
const S = Math.PI;
const W = -QUARTER;

interface PutOptions {
    /** y is where the top goes (a ceiling lamp, a clock), not the bottom. */
    top?: boolean;
    /** One factor, or one per axis (the model's own). */
    scale?: number | Point;
    passable?: boolean;
}

/** A model with its box centre on (x, z), its bottom (or top) at y, facing `facing`. */
function put(
    model: Model,
    x: number,
    y: number,
    z: number,
    facing = N,
    { top = false, scale = 1, passable = false }: PutOptions = {},
): Piece {
    const [x0, x1, y0, y1, z0, z1] = BOXES[model];
    const s = typeof scale === 'number' ? { x: scale, y: scale, z: scale } : scale;
    const yaw = facing - (FRONTS[model] ?? Math.PI);
    const cx = ((x0 + x1) / 2) * s.x;
    const cz = ((z0 + z1) / 2) * s.z;
    // The box centre turned by the yaw: +z goes to (sin, cos), +x to (cos, -sin).
    const wx = cx * Math.cos(yaw) + cz * Math.sin(yaw);
    const wz = -cx * Math.sin(yaw) + cz * Math.cos(yaw);

    return {
        model: `${model}.i3d`,
        position: { x: x - wx, y: top ? y - y1 * s.y : y - y0 * s.y, z: z - wz },
        rotation: { x: 0, y: yaw, z: 0 },
        scale: scale === 1 ? null : s,
        passable,
    };
}

function raw(
    model: string,
    position: Point,
    rotation: Point,
    scale: Point | null = null,
    passable = false,
): Piece {
    return { model: `${model}.i3d`, position, rotation, scale, passable };
}

/** A gap in a wall: its centre along the wall, its width and its top. */
interface Opening {
    at: number;
    width: number;
    top?: number;
}

// One xzed piece of a wall running along x (turned for z), centred on `mid`
// along the wall, `length` long and from y0 to y1.
function wallPiece(
    list: Piece[],
    alongX: boolean,
    line: number,
    mid: number,
    length: number,
    y0: number,
    y1: number,
): void {
    list.push(
        raw(
            'xzed',
            alongX ? { x: mid, y: y0, z: line } : { x: line, y: y0, z: mid },
            { x: 0, y: alongX ? 0 : QUARTER, z: 0 },
            { x: length / 6, y: (y1 - y0) / 2.5, z: 1 },
        ),
    );
}

/**
 * A straight wall on the line x (or z) = `line` from `from` to `to`, `height`
 * high, cut by its openings; each stretch is split into pieces of at most 6 m
 * and runs on half a wall's thickness at both ends to close the corners.
 */
function wall(
    list: Piece[],
    alongX: boolean,
    line: number,
    from: number,
    to: number,
    height: number,
    openings: readonly Opening[] = [],
): void {
    let start = from - WALL / 2;
    const cuts = [...openings].sort((a, b) => a.at - b.at);

    for (const cut of [...cuts, null]) {
        const end = cut ? cut.at - cut.width / 2 : to + WALL / 2;
        const pieces = Math.ceil((end - start) / 6);
        const length = (end - start) / pieces;

        for (let i = 0; i < pieces; i++) {
            wallPiece(list, alongX, line, start + length * (i + 0.5), length, 0, height);
        }

        if (cut) {
            wallPiece(list, alongX, line, cut.at, cut.width, cut.top ?? LINTEL, height);
            start = cut.at + cut.width / 2;
        }
    }
}

/** Floor or ceiling over a rectangle: the 12 x 2.7 m plane laid flat, tiled to fit. */
function slab(
    list: Piece[],
    model: 'plotdrev' | 'plotcihly3',
    x0: number,
    x1: number,
    z0: number,
    z1: number,
    y: number,
): void {
    const columns = Math.ceil((x1 - x0) / 12);
    const rows = Math.ceil((z1 - z0) / 2.7);
    const width = (x1 - x0) / columns;
    const depth = (z1 - z0) / rows;

    for (let i = 0; i < columns; i++) {
        for (let j = 0; j < rows; j++) {
            list.push(
                raw(
                    model,
                    { x: x0 + width * (i + 0.5), y, z: z0 + depth * (j + 0.5) },
                    { x: QUARTER, y: 0, z: 0 },
                    { x: width / 12, y: depth / 2.7, z: 1 },
                ),
            );
        }
    }
}

/** A room: its floor's rectangle and its height. */
export interface RoomBox {
    x0: number;
    x1: number;
    z0: number;
    z1: number;
    height: number;
}

// prettier-ignore
export const ROOMS = {
    hall: { x0: -7, x1: 7, z0: -11, z1: 0, height: HALL_HEIGHT },
    corridor: { x0: -21, x1: 21, z0: 0, z1: 3, height: HEIGHT },
    consulting: { x0: -14, x1: -7, z0: -7, z1: 0, height: HEIGHT },
    lobby: { x0: 7, x1: 14, z0: -7, z1: 0, height: HEIGHT },
    men: { x0: -21, x1: -5, z0: 3, z1: 13, height: HEIGHT },
    theatre: { x0: -5, x1: 5, z0: 3, z1: 12, height: HEIGHT },
    women: { x0: 5, x1: 21, z0: 3, z1: 13, height: HEIGHT },
} as const satisfies Readonly<Record<string, RoomBox>>;

export type RoomKey = keyof typeof ROOMS;

// The doorways, by the wall they cut.
const CONSULTING_DOOR = -10.5;
const LOBBY_DOOR = 10.5;
const HALL_OPENING = 5;
const WARD_DOOR = 13;

function shell(list: Piece[]): void {
    const { hall, corridor, consulting, lobby, men, theatre, women } = ROOMS;

    // The corridor's south side: the consulting room, the hall's opening, the lobby.
    wall(list, true, 0, corridor.x0, consulting.x0, HEIGHT);
    wall(list, true, 0, consulting.x0, hall.x0, HEIGHT, [{ at: CONSULTING_DOOR, width: 1.4 }]);
    wall(list, true, 0, hall.x0, hall.x1, HALL_HEIGHT, [
        { at: 0, width: HALL_OPENING, top: OPENING_TOP },
    ]);

    wall(list, true, 0, hall.x1, lobby.x1, HEIGHT, [{ at: LOBBY_DOOR, width: 1.4 }]);
    wall(list, true, 0, lobby.x1, corridor.x1, HEIGHT);
    // Its north side: the two wards and the theatre between them.
    wall(list, true, 3, men.x0, men.x1, HEIGHT, [{ at: -WARD_DOOR, width: 2 }]);
    wall(list, true, 3, theatre.x0, theatre.x1, HEIGHT, [{ at: 0, width: 2 }]);
    wall(list, true, 3, women.x0, women.x1, HEIGHT, [{ at: WARD_DOOR, width: 2 }]);
    // The north front and the theatre's back wall in front of it.
    wall(list, true, men.z1, men.x0, women.x1, HEIGHT);
    wall(list, true, theatre.z1, theatre.x0, theatre.x1, HEIGHT);
    // The ends and the walls between the rooms.
    wall(list, false, corridor.x0, corridor.z0, men.z1, HEIGHT);
    wall(list, false, corridor.x1, corridor.z0, women.z1, HEIGHT);
    wall(list, false, men.x1, men.z0, men.z1, HEIGHT);
    wall(list, false, women.x0, women.z0, women.z1, HEIGHT);
    // The hall (its walls 4.5 m high), the consulting room and the lobby.
    wall(list, true, hall.z0, hall.x0, hall.x1, HALL_HEIGHT);
    wall(list, false, hall.x0, hall.z0, hall.z1, HALL_HEIGHT);
    wall(list, false, hall.x1, hall.z0, hall.z1, HALL_HEIGHT);
    wall(list, true, consulting.z0, consulting.x0, consulting.x1, HEIGHT);
    wall(list, false, consulting.x0, consulting.z0, consulting.z1, HEIGHT);
    wall(list, true, lobby.z0, lobby.x0, lobby.x1, HEIGHT);
    wall(list, false, lobby.x1, lobby.z0, lobby.z1, HEIGHT);

    for (const [key, room] of Object.entries(ROOMS)) {
        slab(
            list,
            key === 'theatre' ? 'plotcihly3' : 'plotdrev',
            room.x0,
            room.x1,
            room.z0,
            room.z1,
            0,
        );

        slab(list, 'plotcihly3', room.x0, room.x1, room.z0, room.z1, room.height);
    }
}

// ---- The entrance hall ------------------------------------------------------
// The waiting chairs: two rows facing east; people sit on three of them.
const WAITING_ROWS: readonly number[] = [-6.55, -4.75];
const WAITING_Z: readonly number[] = [-9.0, -8.3, -7.6, -6.9, -6.2, -5.5];
const WAITING_SEATS: readonly (readonly [number, number])[] = [
    [-6.55, -8.3],
    [-6.55, -5.5],
    [-4.75, -6.9],
];
// The reception desk along the east side, the receptionist behind it.
const DESK = { x: 4.4, z: -5.5, top: 0.92 } as const;

function seated(list: readonly (readonly [number, number])[], x: number, z: number): boolean {
    return list.some(([sx, sz]) => sx === x && sz === z);
}

function hall(list: Piece[]): void {
    const { z0, x0, x1 } = ROOMS.hall;

    // The street doors, a pair in the middle of the south wall.
    for (const x of [-0.7, 0.7]) {
        list.push(put('plotdrev7', x, 0, z0 + WALL / 2 + 0.06, N));
    }

    list.push(put('xvesak', -2.3, 0, z0 + 0.45, N));
    list.push(put('xobraz10', 2.6, 1.45, z0 + WALL / 2 + 0.02, N, { passable: true }));
    list.push(put('9kober4', -0.6, 0.01, -6.0, E, { passable: true }));

    list.push(put('9pstul2', DESK.x, 0, DESK.z, E));
    list.push(put('telefon', DESK.x + 0.1, DESK.top, DESK.z - 0.9, W));
    list.push(put('xsvetlo 08 on', DESK.x + 0.2, DESK.top, DESK.z + 0.95, W));
    list.push(put('xkniha 1', DESK.x - 0.1, DESK.top, DESK.z - 0.2, W));
    list.push(put('9notes', DESK.x, DESK.top, DESK.z + 0.35, W));
    list.push(put('xstroj01', DESK.x + 0.15, DESK.top, DESK.z + 0.25, W));
    // The records behind the desk, the clock over them.
    list.push(put('9skrinsk', x1 - WALL / 2 - 0.2, 0, DESK.z - 2.0, W));
    list.push(put('xskrin 11', x1 - WALL / 2 - 0.26, 0, DESK.z, W));
    list.push(put('9skrinsk', x1 - WALL / 2 - 0.2, 0, DESK.z + 2.0, W));
    list.push(
        put('xhodiny 2', x1 - WALL / 2 - 0.02, 3.4, DESK.z, W, { top: true, passable: true }),
    );

    for (const x of WAITING_ROWS) {
        for (const z of WAITING_Z) {
            list.push(put('xzidle 03', x, 0, z, E, { passable: seated(WAITING_SEATS, x, z) }));
        }
    }

    list.push(put('xobraz03', x0 + WALL / 2 + 0.01, 1.7, -7.2, E, { passable: true }));
    list.push(put('xobraz09', x0 + WALL / 2 + 0.02, 1.7, -4.4, E, { passable: true }));

    for (const [x, z] of [
        [6.5, -10.5],
        [-6.5, -1.6],
    ] as const) {
        list.push(put('xkvet01', x, 0, z));
    }

    list.push(put('fikus', -6.3, 0, -10.3));
    list.push(put('9tabul', -2.6, 1.5, z0 + WALL / 2 + 0.02, N, { passable: true }));

    // More chairs along the south wall, facing the desk across the hall.
    for (const x of [-5.3, -4.6, -3.9]) {
        list.push(put('xzidle 03', x, 0, z0 + WALL / 2 + 0.3, N));
    }

    // Palms either side of the opening into the corridor.
    for (const x of [-3.2, 3.2]) {
        list.push(put('fikus', x, 0, -0.6));
    }

    for (const [x, z] of [
        [-3.5, -7.5],
        [3.5, -7.5],
        [-3.5, -3.0],
        [3.5, -3.0],
    ] as const) {
        list.push(put('xsvetlo 09 on', x, HALL_HEIGHT, z, N, { top: true, passable: true }));
    }
}

// ---- The corridor, the consulting room and the lift lobby -------------------
const CONSULTING_DESK = { x: -10.5, z: -5.0 } as const;

function corridor(list: Piece[]): void {
    for (const x of [-18, -12, -6, 6, 12, 18]) {
        list.push(put('xsvetlo 10 on', x, HEIGHT, 1.5, N, { top: true, passable: true }));
    }

    for (const x of [-17.6, -17.0, 17.0, 17.6]) {
        list.push(put('xzidle 03', x, 0, 0.42, N));
    }

    list.push(put('xkvet01', -20.6, 0, 0.4));
    list.push(put('xkvet01', 20.6, 0, 0.4));
    list.push(put('xhodiny 2', -3.5, 2.6, 3 - WALL / 2 - 0.02, S, { top: true, passable: true }));
    list.push(put('9tabul', 3.5, 1.2, 3 - WALL / 2 - 0.02, S, { passable: true }));
}

function consulting(list: Piece[]): void {
    const { x0, x1, z0 } = ROOMS.consulting;
    const { x, z } = CONSULTING_DESK;

    // The doctor sits on the desk's south side, facing the door.
    list.push(put('xstul16', x, 0, z, S));
    list.push(put('xzidle 03', x, 0, z - 0.85, N, { passable: true }));
    list.push(put('xzidle 03', x - 0.3, 0, z + 0.95, S));
    list.push(put('xsvetlo 08 on', x - 0.7, 0.78, z - 0.1, N));
    list.push(put('xkniha 1', x + 0.5, 0.78, z, N));
    list.push(put('9notes', x + 0.1, 0.78, z - 0.05, N));
    list.push(put('xobraz10', x, 1.6, z0 + WALL / 2 + 0.01, N, { passable: true }));
    // The examination couch along the west wall, the books and the basin.
    list.push(put('xpostel 2', x0 + 0.8, 0, -2.6, N));
    list.push(put('9skrinsk', x1 - 0.32, 0, -2.0, W));
    list.push(put('xskrin 11', x1 - 0.38, 0, -4.4, W));
    list.push(put('umyvadlo', x0 + 0.5, 0, -5.6, E));
    list.push(put('9lekarna', x0 + WALL / 2 + 0.17, 1.35, -5.6, E, { scale: 1.6 }));
    list.push(put('xkvet01', x1 - 0.5, 0, z0 + 0.5));
    list.push(put('xsvetlo 10 on', x, HEIGHT, -3.5, N, { top: true, passable: true }));
}

function lobby(list: Piece[]): void {
    const { x1, z0 } = ROOMS.lobby;

    // The lifts, shut.
    for (const z of [-2.2, -4.8]) {
        list.push(put('plotdrev7', x1 - WALL / 2 - 0.06, 0, z, W));
    }

    list.push(put('9tabul', x1 - WALL / 2 - 0.02, 1.5, -3.5, W, { passable: true }));
    list.push(put('xlavicka', 10.5, 0, z0 + WALL / 2 + 0.35, N));
    list.push(put('xkvet01', 8.0, 0, z0 + 0.5));
    list.push(put('xsvetlo 10 on', 10.5, HEIGHT, -3.5, N, { top: true, passable: true }));
}

// ---- The wards --------------------------------------------------------------
// A bed's box centre, 5 cm off the wall it has its head against.
const BED_Z = WALL / 2 + 0.05 + 1.21;
// Beds along each ward's north wall, and along its south wall clear of the door.
const NORTH_BEDS: readonly number[] = [-19.6, -17.0, -14.4, -11.8, -9.2, -6.6];
const SOUTH_BEDS: readonly number[] = [-19.6, -17.0, -9.2, -6.6];
const TROLLEY = { x: -12.0, z: 8.0 } as const;

// A ward's piece at the men's x, mirrored for the women's.
function mirror(x: number, women: boolean): number {
    return women ? -x : x;
}

function ward(list: Piece[], women: boolean): void {
    const { z0, z1, x0 } = ROOMS.men;
    const m = (x: number): number => mirror(x, women);

    for (const x of NORTH_BEDS) {
        list.push(put('xpostel 2', m(x), 0, z1 - BED_Z, N));
        list.push(
            put('okno3', m(x), 1.78, z1 - WALL / 2 - 0.01, S, { scale: 0.8, passable: true }),
        );
    }

    for (const x of SOUTH_BEDS) {
        list.push(put('xpostel 2', m(x), 0, z0 + BED_Z, S));
    }

    // Screens between the beds: two single-sided curtains back to back, hung
    // 0.3 m up, out from the wall.
    const screens = [
        ...NORTH_BEDS.slice(1).map((x) => [x - 1.3, z1 - WALL / 2 - 0.05 - 0.98] as const),
        ...[-18.3, -7.9].map((x) => [x, z0 + WALL / 2 + 0.05 + 0.98] as const),
    ];

    for (const [x, z] of screens) {
        const scale = { x: 1, y: 0.42, z: 1 };

        list.push(raw('zavesik', { x: m(x) - 0.01, y: 1.2, z }, { x: 0, y: 0, z: 0 }, scale, true));
        list.push(
            raw('zavesik', { x: m(x) + 0.01, y: 1.2, z }, { x: 0, y: Math.PI, z: 0 }, scale, true),
        );
    }

    // The nurse's trolley in the aisle, a basin and the linen at the ends.
    list.push(put('9vozik', m(TROLLEY.x), 0, TROLLEY.z, N));
    list.push(put('umyvadlo', m(x0 + 0.5), 0, 8.0, women ? W : E));
    list.push(put('9lekarna', m(x0 + WALL / 2 + 0.17), 1.35, 8.0, women ? W : E, { scale: 1.6 }));
    list.push(put('xskrin 11', m(-5.0 - WALL / 2 - 0.26), 0, 7.2, women ? E : W));
    // A chair beside the third bed for whoever visits it.
    list.push(put('xzidle 03', m(-13.45), 0, 10.4, women ? E : W, { passable: true }));

    for (const x of [-17.5, -13.0, -8.5]) {
        list.push(put('xsvetlo 10 on', m(x), HEIGHT, 8.0, N, { top: true, passable: true }));
    }
}

function theatre(list: Piece[]): void {
    const { x0, z1 } = ROOMS.theatre;

    // The bed the patient lies on, under the lamp, its head to the west.
    list.push(put('xpostel 2', 0, 0, 7.8, W));
    list.push(put('xsvetlo 09 on', 0, HEIGHT, 7.8, N, { top: true, scale: 0.9, passable: true }));
    list.push(put('xstul19', 1.7, 0, 9.0, N, { scale: { x: 0.45, y: 1.1, z: 0.45 } }));
    list.push(put('9vozik', -1.8, 0, 9.6, E));
    list.push(put('xkybl 1', 1.4, 0, 6.6));

    for (const x of [-2.6, 2.6]) {
        list.push(put('xskrin 10', x, 0, z1 - WALL / 2 - 0.27, S));
    }

    list.push(put('xskrin 08', 4.3, 0, 10.6, W));

    for (const z of [5.4, 8.6]) {
        list.push(put('umyvadlo', x0 + WALL / 2 + 0.36, 0, z, E));
    }

    list.push(put('9lekarna', x0 + WALL / 2 + 0.17, 1.35, 7.0, E, { scale: 1.6 }));

    for (const z of [5.0, 10.0]) {
        list.push(put('xsvetlo 10 on', 0, HEIGHT, z, N, { top: true, passable: true }));
    }
}

// The janitor's broom and bucket by where he sweeps.
const JANITOR = { x: -8.0, z: 1.6 } as const;

export function pieces(): Piece[] {
    const list: Piece[] = [];

    shell(list);
    hall(list);
    corridor(list);
    consulting(list);
    lobby(list);
    ward(list, false);
    ward(list, true);
    theatre(list);
    list.push(put('xkoste 1', JANITOR.x - 1.2, 0, 0.2, N, { passable: true }));
    list.push(put('xkybl 1', JANITOR.x - 0.7, 0, 0.35));

    return list;
}

export function toWorld(local: Point): Point {
    return offset(ORIGIN, local);
}

// MapObject.create throws for a model name outside [A-Za-z0-9_-. ]: such a
// piece is left out, so the rooms still stand and a mission change never
// fails on it.
const LOADABLE = /^[A-Za-z0-9_\-. ]{1,59}\.(i3d|4ds)$/i;
const skipped = new Set<string>();

function lay(objects: MapObject[]): string | null {
    for (const piece of pieces()) {
        if (!LOADABLE.test(piece.model)) {
            if (!skipped.has(piece.model)) {
                log.error(`Hospital piece ${piece.model} cannot be a map object; left out`);
            }

            skipped.add(piece.model);
            continue;
        }

        const object = tryCreate(
            `Hospital piece ${piece.model}`,
            piece.model,
            toWorld(piece.position),
            piece.rotation,
            optionsFor(piece, WORLD),
        );

        if (!object) {
            return `${piece.model} was refused`;
        }

        objects.push(object);
    }

    return null;
}

const rooms = new MapStructure('the hospital', lay);

export function isBuilt(): boolean {
    return rooms.isBuilt();
}

export function build(): void {
    rooms.build();
}

/** A mission change has already removed the objects. */
export function forget(): void {
    rooms.forget();
}

export function teardown(): void {
    rooms.teardown();
}

/** The room a local point stands in (a margin in metres around each), or null. */
export function roomAt(local: Point, margin = 0): RoomKey | null {
    for (const [key, room] of Object.entries(ROOMS) as [RoomKey, RoomBox][]) {
        if (
            local.x >= room.x0 - margin &&
            local.x <= room.x1 + margin &&
            local.z >= room.z0 - margin &&
            local.z <= room.z1 + margin &&
            local.y >= -3 &&
            local.y <= room.height + 3
        ) {
            return key;
        }
    }

    return null;
}

/** Inside the hospital (a margin in metres around its rooms). */
export function inRoom(position: Point, margin = 0): boolean {
    return roomAt(relative(ORIGIN, position), margin) !== null;
}

function pose(position: Point, heading: number): Placement {
    return { position: toWorld(position), heading };
}

/** Inside the street doors, facing into the hall (local). */
export const ARRIVAL: Readonly<Placement> = Object.freeze({
    position: { x: 0, y: 0.05, z: ROOMS.hall.z0 + 1.3 },
    heading: N,
});

export function arrival(): Placement {
    return pose(ARRIVAL.position, ARRIVAL.heading);
}

/** How a person holds their place: standing, sitting on a chair, lying on a bed. */
export type Posture = 'stand' | 'sit' | 'lie';

/** Where one of the people inside stands, local. */
export interface Spot extends Placement {
    posture: Posture;
}

const stand = (x: number, z: number, heading: number): Spot => ({
    position: { x, y: 0.05, z },
    heading,
    posture: 'stand',
});
// A seated NPC stands on the chair's box centre, facing out of it, as the
// bar's drinkers do; its sitting clip lowers it onto the seat.
const sit = (x: number, z: number, heading: number): Spot => ({
    position: { x, y: 0.02, z },
    heading,
    posture: 'sit',
});
// A lying patient's root is on the mattress (0.7 m up) at the bed's middle;
// the lying clip (SamZrLehS) puts the head 0.6 m behind it and the feet
// 0.7 m ahead, so it faces the foot of the bed.
const lie = (x: number, z: number, heading: number): Spot => ({
    position: { x, y: 0.7, z },
    heading,
    posture: 'lie',
});

// The middle of a north bed's mattress, and of a south one's.
const NORTH_MATTRESS = ROOMS.men.z1 - BED_Z + 0.02;
const SOUTH_MATTRESS = ROOMS.men.z0 + BED_Z - 0.02;

// prettier-ignore
export const SPOTS = {
    receptionist: stand(DESK.x + 1.05, DESK.z, W),
    waiting: WAITING_SEATS.map(([x, z]) => sit(x, z, E)),
    janitor: stand(JANITOR.x, JANITOR.z, E),
    doctor: sit(CONSULTING_DESK.x, CONSULTING_DESK.z - 0.85, N),
    surgeon: stand(0, 6.6, N),
    theatreNurse: stand(1.0, 8.75, -2.2),
    orderly: stand(ROOMS.theatre.x0 + WALL / 2 + 1.0, 8.6, W),
    onTable: lie(0.02, 7.8, E),
    menNurse: stand(TROLLEY.x, TROLLEY.z + 0.85, S),
    womenNurse: stand(-TROLLEY.x, TROLLEY.z + 0.85, S),
    men: [lie(-19.6, NORTH_MATTRESS, S), lie(-14.4, NORTH_MATTRESS, S), lie(-6.6, SOUTH_MATTRESS, N)],
    women: [lie(17.0, NORTH_MATTRESS, S), lie(9.2, SOUTH_MATTRESS, N)],
    bedside: sit(-13.45, 10.4, W),
} as const satisfies Readonly<Record<string, Spot | readonly Spot[]>>;

/** A local spot in the world. */
export function placed(spot: Placement): Placement {
    return pose(spot.position, spot.heading);
}
