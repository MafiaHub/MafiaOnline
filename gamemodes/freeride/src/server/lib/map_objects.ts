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
