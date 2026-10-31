// Single map objects a module keeps standing in the loaded mission, each
// under a key, swapped when what should stand there changes (the rum still's
// gear, its burst pot after a blast). The mission change removes them; the
// next sync places them again.
import { tryCreate, type ObjectOptions } from './map_objects';
import type { Point } from './util';

export interface ObjectSpec {
    model: string;
    position: Point;
    /** Euler radians; a turn of `yaw` about y when omitted. */
    rotation?: Point;
    yaw?: number;
    scale?: number | Point;
}

interface Placed {
    spec: ObjectSpec;
    object: MapObject | null;
}

export class PlacedObjects<K> {
    private placed = new Map<K, Placed>();
    private generation = -1;

    /**
     * `what` reads in the log line of a refused object. A spec the server
     * refused is not tried again until it changes or the mission does.
     */
    constructor(
        private readonly what: string,
        private readonly virtualWorld: number,
    ) {}

    /** Stands each key's spec (the same object stays while it is the same spec); null removes it. */
    sync(wanted: Iterable<readonly [K, ObjectSpec | null]>): void {
        if (!World.isReady()) {
            return;
        }

        const generation = World.getMissionGeneration();

        if (generation !== this.generation) {
            // The mission change removed them.
            this.placed = new Map();
            this.generation = generation;
        }

        for (const [key, spec] of wanted) {
            const have = this.placed.get(key);

            if (have && have.spec === spec) {
                continue;
            }

            have?.object?.destroy();
            this.placed.delete(key);

            if (spec) {
                this.placed.set(key, { spec, object: this.create(spec) });
            }
        }
    }

    /** The mission change already removed them. */
    forget(): void {
        this.placed = new Map();
    }

    clear(): void {
        for (const { object } of this.placed.values()) {
            object?.destroy();
        }

        this.placed = new Map();
    }

    private create(spec: ObjectSpec): MapObject | null {
        const rotation = spec.rotation ?? { x: 0, y: spec.yaw ?? 0, z: 0 };
        const options: ObjectOptions = { virtualWorld: this.virtualWorld };

        if (spec.scale) {
            options.scale = spec.scale;
        }

        return tryCreate(
            `${this.what} ${spec.model}`,
            spec.model,
            spec.position,
            rotation,
            options,
        );
    }
}
