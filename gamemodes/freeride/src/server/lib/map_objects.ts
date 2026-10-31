// Rooms built from stock models (MapObject) where nobody stumbles on them:
// the back-room bar (lib/bar_room.ts), the county jail (lib/prison.ts) and
// the homes (lib/house_rooms.ts). Each stands for one mission: it is built
// once every client can receive it, tried again a few times while pieces are
// refused, and gone with the mission.
import * as log from './log';
import { Delay } from './timers';
import type { Point } from './util';

const RETRY_MS = 3000;
const MAX_FAILURES = 10;

export const QUARTER = Math.PI / 2;

export type ObjectOptions = NonNullable<Parameters<typeof MapObject.create>[3]>;

/** One model of a room, in the room's own frame. */
export interface Piece {
    model: string;
    position: Point;
    /** Euler radians about x, y (up) and z. */
    rotation: Point;
    scale?: Point | null;
    /** Nothing collides with it (lamps, rugs, pictures). */
    passable?: boolean;
}

/** A piece turned by `yaw` about y, standing upright or (flat) laid down. */
export function standing(
    model: string,
    position: Point,
    yaw: number,
    {
        flat = false,
        scale = null,
        passable = false,
    }: { flat?: boolean; scale?: Point | null; passable?: boolean } = {},
): Piece {
    return { model, position, rotation: { x: flat ? QUARTER : 0, y: yaw, z: 0 }, scale, passable };
}

// How far a piece hung on a wall (a picture, a clock, a wall cabinet) keeps
// its back off the wall's face: clear of it, so no face of it ever lies in
// the wall's plane and flickers, and close enough that nothing floats.
export const HANG = 0.005;

// plotdrev7.4ds, the white steel door the built rooms are left by: one slab
// 1.4 m wide, 2.4 m high and 0.1 m thick, finished on both faces (@VRAT01).
// Its origin is on its hinge edge and its back face: x -1.397..0.003,
// y -1.165..1.235, z 0..0.1 (the LOD0 mesh).
export const STEEL_DOOR = Object.freeze({
    model: 'plotdrev7',
    x0: -1.397,
    x1: 0.003,
    y0: -1.165,
    y1: 1.235,
    depth: 0.1,
});
// A door set into a wall stands DOOR_RECESS back from the wall's face, so no
// face of it lies in the wall's plane and it never stands out into the room;
// the wall laps DOOR_LAP over its sides and top, so no gap shows round it,
// and it stands DOOR_SINK into the floor, its underside clear of the floor's
// plane.
export const DOOR_RECESS = 0.02;
export const DOOR_LAP = 0.01;
export const DOOR_SINK = 0.01;

/** The gap a wall leaves for a door, along the wall, and the gap's top. */
export interface DoorCut {
    from: number;
    to: number;
    top: number;
}

/**
 * The steel door set into a wall on z = const (alongX) or x = const, whose
 * face towards the room is at `face`, the room lying on its `side` (+1 or -1
 * along the wall's normal); `mid` is the door's middle along the wall and
 * `mirror` turns it round (the other leaf of a pair). Returns the door and the
 * cut its wall must leave for it, DOOR_LAP smaller than the door all round.
 */
export function steelDoor(
    alongX: boolean,
    face: number,
    side: 1 | -1,
    mid: number,
    { floor = 0, mirror = false }: { floor?: number; mirror?: boolean } = {},
): { door: Piece; cut: DoorCut } {
    const { model, x0, x1, y0, y1, depth } = STEEL_DOOR;
    const yaw = (alongX ? 0 : QUARTER) + (mirror ? Math.PI : 0);
    const cx = (x0 + x1) / 2;
    const cz = depth / 2;
    // The box centre turned by the yaw: +z goes to (sin, cos), +x to (cos, -sin).
    const wx = cx * Math.cos(yaw) + cz * Math.sin(yaw);
    const wz = -cx * Math.sin(yaw) + cz * Math.cos(yaw);
    const across = face - side * (DOOR_RECESS + depth / 2);
    const centre = alongX ? { x: mid, z: across } : { x: across, z: mid };
    const half = (x1 - x0) / 2;
    const bottom = floor - DOOR_SINK;

    return {
        door: {
            model,
            position: { x: centre.x - wx, y: bottom - y0, z: centre.z - wz },
            rotation: { x: 0, y: yaw, z: 0 },
            scale: null,
            passable: false,
        },
        cut: {
            from: mid - half + DOOR_LAP,
            to: mid + half - DOOR_LAP,
            top: bottom + (y1 - y0) - DOOR_LAP,
        },
    };
}

// The facing planes (plotcihly1, plotcihly4: 12 x 2.7 m, finished on both
// sides) are never cut short or squeezed to fit round a door, which would
// stretch their texture: see facing().
const FACING_WIDTH = 12;
const FACING_HEIGHT = 2.7;

/**
 * The facing planes of a wall on z = `line` (alongX) or x = `line`, from
 * `from` to `to` along it, scaled `sx` along the wall like the room's other
 * facings, with a gap for a door. Below the door's top the planes run out
 * from either side of the gap, above it on from its left edge, every one at
 * its full size: the two rows meet at the door's top, which must be less
 * than 2.7 m above the floor and more than 2.7 m under the ceiling's top, and
 * what runs on past the room's ends, under the floor and over the ceiling is
 * hidden behind the other facings and the slabs. Next to each other the planes
 * repeat their texture without a seam. Model names come back bare, as
 * steelDoor's: a room whose pieces carry the extension adds it (named()).
 */
export function facing(
    model: string,
    alongX: boolean,
    line: number,
    from: number,
    to: number,
    cut: DoorCut,
    sx = 1,
): Piece[] {
    const width = FACING_WIDTH * sx;
    const list: Piece[] = [];
    const plane = (mid: number, y: number): void => {
        list.push({
            model,
            position: alongX ? { x: mid, y, z: line } : { x: line, y, z: mid },
            rotation: { x: 0, y: alongX ? 0 : QUARTER, z: 0 },
            scale: sx === 1 ? null : { x: sx, y: 1, z: 1 },
            passable: false,
        });
    };
    const below = cut.top - FACING_HEIGHT / 2;
    const above = cut.top + FACING_HEIGHT / 2;

    for (let edge = cut.from; edge > from; edge -= width) {
        plane(edge - width / 2, below);
        plane(edge - width / 2, above);
    }

    for (let edge = cut.to; edge < to; edge += width) {
        plane(edge + width / 2, below);
    }

    for (let edge = cut.from; edge < to; edge += width) {
        plane(edge + width / 2, above);
    }

    return list;
}

/** The pieces with ".i3d" on their model names, for the rooms that name them so. */
export function named(pieces: readonly Piece[]): Piece[] {
    return pieces.map((piece) => ({ ...piece, model: `${piece.model}.i3d` }));
}

/** The options MapObject.create takes for a piece; a scale only when it has one. */
export function optionsFor(piece: Piece, virtualWorld: number): ObjectOptions {
    const options: ObjectOptions = { virtualWorld, collision: !piece.passable };

    if (piece.scale) {
        options.scale = piece.scale;
    }

    return options;
}

/** Whether this server can place map objects at all. */
export function available(): boolean {
    return typeof MapObject !== 'undefined' && typeof MapObject.create === 'function';
}

/** A room's local point in the world, for a room placed unturned at `origin`. */
export function offset(origin: Point, local: Point): Point {
    return { x: origin.x + local.x, y: origin.y + local.y, z: origin.z + local.z };
}

/** The other way round: a world position in the room's frame. */
export function relative(origin: Point, position: Point): Point {
    return { x: position.x - origin.x, y: position.y - origin.y, z: position.z - origin.z };
}

/**
 * MapObject.create, which throws for a model name it cannot load: one bad
 * piece must never take the gamemode down, so it is logged (as `what`) and
 * comes back null, as one refused at the object limit does.
 */
export function tryCreate(
    what: string,
    model: string,
    position: Point,
    rotation: Point,
    options: ObjectOptions,
): MapObject | null {
    try {
        return MapObject.create(model, position, rotation, options);
    } catch (caught) {
        log.error(`${what} was refused: ${log.describe(caught)}`);

        return null;
    }
}

/**
 * Places the pieces into `objects`. Returns null once the room stands, or
 * what was refused; the objects placed so far are then removed and the
 * whole room is tried again a little later.
 */
export type Layout = (objects: MapObject[]) => string | null;

/** A room of map objects for the loaded mission. */
export class MapStructure {
    readonly objects: MapObject[] = [];
    private built = false;
    private failures = 0;
    private readonly retry = new Delay();

    /** `name` reads in "Could not build <name>"; `onReset` drops state kept about the objects. */
    constructor(
        private readonly name: string,
        private readonly layout: Layout,
        private readonly onReset: () => void = () => {},
    ) {}

    isBuilt(): boolean {
        return this.built;
    }

    /** Builds it afresh for the loaded mission once every client can receive it. */
    build(): void {
        this.retry.cancel();
        this.clear();
        this.failures = 0;
        this.attempt();
    }

    /** A mission change has already removed the objects. */
    forget(): void {
        this.retry.cancel();
        this.objects.length = 0;
        this.onReset();
        this.built = false;
    }

    teardown(): void {
        this.retry.cancel();
        this.clear();
    }

    private clear(): void {
        for (const object of this.objects) {
            object.destroy();
        }

        this.objects.length = 0;
        this.onReset();
        this.built = false;
    }

    private attempt(): void {
        if (this.built || !available() || this.failures >= MAX_FAILURES) {
            return;
        }

        if (!World.isReady()) {
            this.retry.start(RETRY_MS, () => this.attempt());

            return;
        }

        const refused = this.layout(this.objects);

        if (refused === null) {
            this.built = true;
            this.failures = 0;

            return;
        }

        this.clear();

        if (++this.failures >= MAX_FAILURES) {
            log.error(`Could not build ${this.name}: ${refused}`);
        } else {
            this.retry.start(RETRY_MS, () => this.attempt());
        }
    }
}
