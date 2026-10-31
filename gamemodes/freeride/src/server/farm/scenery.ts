// The plants as map objects every client sees (no collision: you walk
// through the rows), remade when a plant changes its look, and the hay bales
// that mark the field out.
import { tryCreate } from '../lib/map_objects';
import { MAIN_WORLD } from '../lib/util';
import { lookOf } from './plant';
import { siteNow } from './sites';
import { field } from './state';

// models/s_ker.4ds: the countryside's own bush (75 in FREEKRAJINA), 1.7 m
// across and 1.33 m tall from its base at the origin; scaled per stage.
const PLANT_MODEL = 's_ker.i3d';
// models/baliksena.4ds: the round hay bale the countryside's fields carry.
const BALE_MODEL = 'baliksena.i3d';
const RETRY_MS = 2000;

interface Bush {
    object: MapObject | null;
    /** The look it was made with; null when it was refused. */
    look: string | null;
    retryAt: number;
}

// Plot id -> its bush.
const bushes = new Map<number, Bush>();
let bales: MapObject[] = [];
let generation = -1;

export function sync(now: number): void {
    if (!World.isReady()) {
        return;
    }

    const site = siteNow();

    if (World.getMissionGeneration() !== generation) {
        // The mission change removed them.
        forget();
        generation = World.getMissionGeneration();
    }

    if (!bales.length) {
        bales = site.bales
            .map((spot, index) =>
                tryCreate(
                    'A farm bale',
                    BALE_MODEL,
                    spot,
                    { x: 0, y: index * 1.3, z: 0 },
                    {
                        virtualWorld: MAIN_WORLD,
                    },
                ),
            )
            .filter((bale) => bale !== null);
    }

    for (const [plot, bush] of bushes) {
        if (!field.has(plot)) {
            bush.object?.destroy();
            bushes.delete(plot);
        }
    }

    for (const plant of field.values()) {
        const scale = lookOf(plant);
        const look = `${scale.x}:${scale.y}`;
        const bush = bushes.get(plant.plot);

        if (bush && (bush.look === look || now < bush.retryAt)) {
            continue;
        }

        bush?.object?.destroy();
        const spot = site.plots[plant.plot];
        const object = tryCreate(
            'A tobacco plant',
            PLANT_MODEL,
            { x: spot.x, y: spot.y - 0.05, z: spot.z },
            { x: 0, y: plant.yaw, z: 0 },
            { virtualWorld: MAIN_WORLD, collision: false, scale },
        );

        bushes.set(plant.plot, { object, look: object ? look : null, retryAt: now + RETRY_MS });
    }
}

/** Takes the plot's bush away at once. */
export function uproot(plot: number): void {
    bushes.get(plot)?.object?.destroy();
    bushes.delete(plot);
}

/** The mission took the objects with it. */
export function forget(): void {
    bushes.clear();
    bales = [];
}

export function clear(): void {
    for (const bush of bushes.values()) {
        bush.object?.destroy();
    }

    for (const bale of bales) {
        bale.destroy();
    }

    forget();
}
