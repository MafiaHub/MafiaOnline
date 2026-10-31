// The county jail: a cell block built from stock models (MapObject) where
// nobody can stumble on it. It stands 150 m under the empty fields north of
// Lost Heaven, more than 500 m from the nearest road on every map, and exists
// only in its own virtual world, so players on the street never receive it.
//
// Local coordinates: x runs along the corridor, z toward the cells, y up; the
// origin is on the floor in the middle of the hall. The hall is 24 by 8.1 m
// and 2.5 m high; three cells of 7.24 by 3 m line its north side, the
// warden's desk stands at the west end, and the east end of the corridor is
// the visiting area, with a bench and the way out.
//
// The models, all from the stock a2.dta, so every map has them:
//  xzed.4ds        a 6 x 2.5 m wall box painted white over a green dado
//                  (0XEX1), finished on both faces: the walls
//  plotdrev.4ds    a 12 x 2.7 m double-sided plane of weathered boards
//                  (PRKNA01): the floor
//  plotcihly3.4ds  the same plane in bare concrete (^BETZED1): the ceiling;
//                  plotcihly1 (bare brick, ^PLOT01) and plotcihly4 (stained
//                  plaster, ^PLOT03) face the end walls
//  plot08.4ds      a 3.62 x 3.6 m panel of steel bars (colour-keyed 0STADPL):
//                  each cell front is one fixed panel and one sliding door
//  xpostel 3.4ds   a plank bunk hung from the wall on chains, and
//  xv zachod1.4ds  a bare toilet, both furniture of the stock prison scene
//                  MISE17-VEZENI, with its xv spina2 floor stains
//  2zarc.4ds       the bare bulb on a cord from Norman's cell (02NORMANCELA)
//  stulprac.4ds    a steel office desk, and xzidle 01.4ds a chair: the post
//  xlavicka.4ds    a wooden bench, and xbojler.4ds a boiler: the visiting area
//  plotdrev7.4ds   a steel door with a notice: the way out
// No stock model is a plain tiled floor on its own: the tiled floors in
// models/ belong to whole rooms or buildings (mor2, 9dum4), and the prison
// scene's own floor is part of its mission scene.4ds, which map objects
// cannot load. Every piece is turned by 0 or a quarter turn about y, or laid
// flat, and is symmetric about its own origin where the turn's sign would
// matter.
import {
    MapStructure,
    QUARTER,
    available,
    offset,
    optionsFor,
    relative,
    standing,
    tryCreate,
    type Piece,
} from './map_objects';
import { distance, type Placement, type Point } from './util';

export { available };

const ORIGIN: Point = Object.freeze({ x: -400, y: -150, z: 1500 });

export const WORLD = 0x50000000;
const HEIGHT = 2.5;
const HALL_X = 12;
const HALL_Z = 4.05;
const CELL_FRONT = 1.05;
const CELL_WIDTH = 7.24;
const PANEL = 3.62;
// The bar panel is 3.6 m tall; it stands 1.13 m into the floor so its top
// rail sits just under the ceiling.
const BARS_Y = HEIGHT - 1.8 - 0.03;
// A door runs on the corridor side of the fixed panel it slides over.
const DOOR_Z = CELL_FRONT - 0.14;
const DOOR_SPEED = 1.5;

const WALL = 'xzed.i3d';
const SLAB = 'plotcihly3.i3d';
const BOARDS = 'plotdrev.i3d';
const BARS = 'plot08.i3d';

/** One cell of the block; positions are local (toWorld places them). */
export interface Cell {
    index: number;
    /** The cell's span along the corridor. */
    x0: number;
    x1: number;
    /** Where a prisoner is put. */
    spawn: Placement;
    door: { closed: Point; open: Point };
    /** Where the warden stands to work the lock. */
    lock: Point;
    /** Where he stops on his rounds to look into the cell. */
    round: Point;
}

function cellAt(index: number): Cell {
    const x0 = -HALL_X + 1.14 + index * CELL_WIDTH;

    return {
        index,
        x0,
        x1: x0 + CELL_WIDTH,
        spawn: { position: { x: x0 + 4.2, y: 0.05, z: 2.4 }, heading: Math.PI },
        door: {
            closed: { x: x0 + PANEL * 1.5, y: BARS_Y, z: DOOR_Z },
            open: { x: x0 + PANEL / 2, y: BARS_Y, z: DOOR_Z },
        },
        // Where the warden stands to work the lock: in the corridor, at
        // arm's length from the door's latch edge by the partition.
        lock: { x: x0 + PANEL * 2 - 0.45, y: 0, z: DOOR_Z - 0.6 },
        // Where he stops on his rounds to look into the cell.
        round: { x: x0 + CELL_WIDTH / 2, y: 0, z: CELL_FRONT - 1.3 },
    };
}

export const CELLS: readonly Cell[] = [0, 1, 2].map(cellAt);
const POST: Placement = { position: { x: -9, y: 0, z: -2.1 }, heading: 0 };
const EXIT: Point = { x: 10, y: 0, z: -3.2 };
// Where a visitor arrives, facing down the corridor.
const VISIT: Placement = { position: { x: 6.5, y: 0.05, z: -2.0 }, heading: -QUARTER };

// Hanging bulbs and floor stains are decoration: walking through them is right.
const PASSABLE = new Set(['2zarc.i3d', 'xv spina2.i3d']);

function piece(model: string, position: Point, yaw: number, flat = false): Piece {
    return standing(model, position, yaw, { flat, passable: PASSABLE.has(model) });
}

function fixedPieces(): Piece[] {
    const list: Piece[] = [];

    for (const z of [-HALL_Z - 0.12, HALL_Z + 0.12]) {
        for (const x of [-9, -3, 3, 9]) {
            list.push(piece(WALL, { x, y: 0, z }, 0));
        }
    }

    for (const x of [-HALL_X - 0.12, HALL_X + 0.12]) {
        for (const z of [-3, 3]) {
            list.push(piece(WALL, { x, y: 0, z }, QUARTER));
        }
    }

    for (const x of [-6, 6]) {
        for (const z of [-2.7, 0, 2.7]) {
            list.push(piece(BOARDS, { x, y: 0, z }, 0, true));
            list.push(piece(SLAB, { x, y: HEIGHT, z }, 0, true));
        }
    }

    // Facings 5 cm in front of the end walls; they run on through the long
    // walls and the ceiling where nobody sees them.
    list.push(piece('plotcihly4.i3d', { x: -HALL_X + 0.05, y: 1.35, z: 0 }, QUARTER));
    list.push(piece('plotcihly1.i3d', { x: HALL_X - 0.05, y: 1.35, z: 0 }, QUARTER));

    // The ends of the cell row: short walls closing the corners, and the
    // partitions, which run on through the back wall where nobody sees them.
    for (const x of [-HALL_X - 1.86, HALL_X + 1.86]) {
        list.push(piece(WALL, { x, y: 0, z: CELL_FRONT }, 0));
    }

    for (const cell of CELLS) {
        list.push(piece(WALL, { x: cell.x0, y: 0, z: HALL_Z }, QUARTER));
        list.push(piece(BARS, { x: cell.x0 + PANEL / 2, y: BARS_Y, z: CELL_FRONT }, 0));
        list.push(piece('xpostel 3.i3d', { x: cell.x0 + 0.92, y: 0.5, z: 2.6 }, 0));
        list.push(piece('xv zachod1.i3d', { x: cell.x1 - 0.6, y: 0.33, z: 3.6 }, 0));
        list.push(piece('xv spina2.i3d', { x: cell.x0 + 5.4, y: 0.01, z: 3.0 }, 0));
        list.push(piece('2zarc.i3d', { x: cell.x0 + PANEL, y: HEIGHT - 0.41, z: 2.6 }, 0));
    }

    for (const x of [-6, 0, 6]) {
        list.push(piece('2zarc.i3d', { x, y: HEIGHT - 0.41, z: -1.5 }, 0));
    }

    list.push(piece(WALL, { x: CELLS[CELLS.length - 1].x1, y: 0, z: HALL_Z }, QUARTER));
    list.push(piece('stulprac.i3d', { x: -9, y: 0.85, z: -3.3 }, 0));
    list.push(piece('xzidle 01.i3d', { x: -7.2, y: 0.53, z: -3.4 }, 0));
    list.push(piece('plotdrev7.i3d', { x: 10.7, y: 1.16, z: -HALL_Z }, 0));
    list.push(piece('xlavicka.i3d', { x: 6.5, y: 0, z: -HALL_Z + 0.25 }, 0));
    list.push(piece('xbojler.i3d', { x: 11.5, y: 0, z: 0.3 }, 0));
    list.push(piece('xv spina2.i3d', { x: 2, y: 0.01, z: -2.2 }, 0));

    return list;
}

/** Every piece of the jail; the sliding doors, closed, are the last ones, in cell order. */
export function pieces(): Piece[] {
    return [...fixedPieces(), ...CELLS.map((cell) => piece(BARS, cell.door.closed, 0))];
}

export function toWorld(local: Point): Point {
    return offset(ORIGIN, local);
}

interface Door {
    object: MapObject;
    open: boolean;
    since: number;
}

const doors = new Map<number, Door>();

function lay(objects: MapObject[]): string | null {
    for (const entry of pieces()) {
        const object = tryCreate(
            `Jail piece ${entry.model}`,
            entry.model,
            toWorld(entry.position),
            entry.rotation,
            optionsFor(entry, WORLD),
        );

        if (!object) {
            return `${entry.model} was refused`;
        }

        objects.push(object);
    }

    // The doors are the last pieces, in cell order.
    const first = objects.length - CELLS.length;

    for (const cell of CELLS) {
        doors.set(cell.index, { object: objects[first + cell.index], open: false, since: 0 });
    }

    return null;
}

const jail = new MapStructure('the jail', lay, () => doors.clear());

export function isBuilt(): boolean {
    return jail.isBuilt();
}

/** Builds the jail for the loaded mission once every client can receive it. */
export function build(): void {
    jail.build();
}

/** A mission change has already removed the objects. */
export function forget(): void {
    jail.forget();
}

export function teardown(): void {
    jail.teardown();
}

function setDoor(cell: Cell, open: boolean): boolean {
    const door = doors.get(cell.index);

    if (!door || door.open === open) {
        return false;
    }

    door.open = open;
    door.since = Date.now();
    door.object.moveTo(toWorld(open ? cell.door.open : cell.door.closed), DOOR_SPEED);

    return true;
}

/** Slides the cell's door open; false when it already is (or the jail is not built). */
export function openDoor(cell: Cell): boolean {
    return setDoor(cell, true);
}

export function closeDoor(cell: Cell): boolean {
    return setDoor(cell, false);
}

export function doorOpen(cell: Cell): boolean {
    return doors.get(cell.index)?.open ?? false;
}

/** How long the cell's door has stood open, in ms; 0 while it is shut. */
export function doorOpenFor(cell: Cell): number {
    const door = doors.get(cell.index);

    return door?.open ? Date.now() - door.since : 0;
}

/** Inside the cell, behind its bars; margin widens the box. */
export function inCell(cell: Cell, position: Point, margin = 0): boolean {
    const local = relative(ORIGIN, position);

    return (
        local.x >= cell.x0 - margin &&
        local.x <= cell.x1 + margin &&
        local.z >= CELL_FRONT - margin &&
        local.z <= HALL_Z + margin &&
        Math.abs(local.y) <= 3
    );
}

export function inAnyCell(position: Point, margin = 0): boolean {
    return CELLS.some((cell) => inCell(cell, position, margin));
}

export function inHall(position: Point, margin = 0): boolean {
    const local = relative(ORIGIN, position);

    return (
        Math.abs(local.x) <= HALL_X + margin &&
        Math.abs(local.z) <= HALL_Z + margin &&
        local.y >= -3 &&
        local.y <= HEIGHT + 3
    );
}

export function exit(): Point {
    return toWorld(EXIT);
}

export function atExit(position: Point, reach: number): boolean {
    return distance(position, exit()) <= reach;
}

function pose(entry: Placement): Placement {
    return { position: toWorld(entry.position), heading: entry.heading };
}

export function spawnOf(cell: Cell): Placement {
    return pose(cell.spawn);
}

export function lockOf(cell: Cell): Point {
    return toWorld(cell.lock);
}

/** The latch edge of the cell's door, which the warden faces. */
export function latchOf(cell: Cell): Point {
    return toWorld({ x: cell.lock.x, y: 0, z: DOOR_Z });
}

/** Where the warden stops on his rounds, and the point in the cell he looks at. */
export function roundOf(cell: Cell): { position: Point; look: Point } {
    return {
        position: toWorld(cell.round),
        look: toWorld({ x: cell.round.x, y: 0, z: cell.spawn.position.z }),
    };
}

/** The warden's desk. */
export function post(): Placement {
    return pose(POST);
}

export function visit(): Placement {
    return pose(VISIT);
}
