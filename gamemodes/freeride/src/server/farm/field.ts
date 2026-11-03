// The field: its plots and the plants growing on them. Rain waters them all;
// otherwise each stage needs a farm truck's hose. Dry for too long, eaten by
// hornworms or left ripe too long, a plant loses quality until it withers.
import { flatDistance, type Point } from '../lib/util';
import { crewTag, notice } from './owners';
import { DRY_GRACE_MS, RIPE, STAGE_MS } from './plant';
import { uproot } from './scenery';
import { siteNow, type Plot } from './sites';
import { changed, field, growing, type Plant } from './state';

export const MAX_PLANTS = 8;
export const SEEDLING_PRICE = 10;
const PLANT_REACH = 3;
const WILT_PER_MS = 15 / 60_000;
const PEST_PER_MS = 20 / 60_000;
const PEST_CHANCE = 0.3;
const RIPE_KEEP_MS = 15 * 60_000;
const ROT_PER_MS = 10 / 60_000;

let wasRaining = false;

export function inField(position: Point): boolean {
    const area = siteNow().field;

    return (
        position.x >= area.minX &&
        position.x <= area.maxX &&
        position.z >= area.minZ &&
        position.z <= area.maxZ &&
        Math.abs(position.y - area.center.y) < 6
    );
}

export function plotPosition(plot: number): Point {
    const spot = siteNow().plots[plot];

    return { x: spot.x, y: spot.y, z: spot.z };
}

export function ownPlants(key: string | null): Plant[] {
    return key ? [...field.values()].filter((plant) => plant.ownerKey === key) : [];
}

/** The plants within `reach`, nearest first. */
export function plantsNear(position: Point, reach: number): { plant: Plant; gap: number }[] {
    return [...field.values()]
        .map((plant) => ({ plant, gap: flatDistance(plotPosition(plant.plot), position) }))
        .filter((entry) => entry.gap <= reach)
        .sort((a, b) => a.gap - b.gap);
}

/** The plot the player stands on, when it is free. */
export function freePlot(position: Point): Plot | null {
    const nearest = siteNow()
        .plots.map((spot) => ({ spot, gap: flatDistance(spot, position) }))
        .sort((a, b) => a.gap - b.gap)[0];

    return nearest && nearest.gap <= PLANT_REACH && !field.has(nearest.spot.id)
        ? nearest.spot
        : null;
}

function rollPest(stage: number): number | null {
    return stage > 0 && Math.random() < PEST_CHANCE
        ? STAGE_MS[stage] * (0.2 + Math.random() * 0.6)
        : null;
}

export function isRaining(): boolean {
    const weather = World.getWeather();

    return weather.weather === 'rain' && (weather.intensity ?? 50) >= 20;
}

/** A seedling on the plot, the player's record's. */
export function sow(player: Player, key: string, plot: Plot): void {
    field.set(plot.id, {
        plot: plot.id,
        ownerKey: key,
        ownerName: player.nickname,
        ownerCrew: crewTag(player),
        stage: 0,
        grown: 0,
        dry: true,
        dryMs: 0,
        pest: false,
        pestAt: null,
        ripeMs: 0,
        quality: 100,
        yaw: Math.random() * Math.PI * 2,
    });

    changed();
}

export function water(plant: Plant): void {
    plant.dry = false;
    plant.dryMs = 0;
    changed();
}

export function remove(plant: Plant): void {
    field.delete(plant.plot);
    uproot(plant.plot);
    changed();
}

function growPlant(plant: Plant, dt: number, raining: boolean): void {
    if (plant.stage >= RIPE) {
        plant.ripeMs += dt;

        if (plant.ripeMs > RIPE_KEEP_MS) {
            plant.quality -= ROT_PER_MS * dt;
        }

        return;
    }

    if (plant.dry && raining) {
        water(plant);
    }

    if (plant.dry) {
        plant.dryMs += dt;

        if (plant.dryMs > DRY_GRACE_MS) {
            plant.quality -= WILT_PER_MS * dt;
        }
    }

    if (plant.pest) {
        plant.quality -= PEST_PER_MS * dt;
    }

    if (plant.dry || plant.pest) {
        return;
    }

    plant.grown += dt;

    if (plant.pestAt !== null && plant.grown >= plant.pestAt) {
        plant.pest = true;
        plant.pestAt = null;
        changed();
        notice(
            plant.ownerKey,
            'pest',
            'Hornworms are eating one of your plants! Hold G at it to pick them off.',
            'bad',
        );
    }

    if (plant.grown < STAGE_MS[plant.stage]) {
        return;
    }

    plant.stage++;
    plant.grown = 0;
    changed();

    if (plant.stage >= RIPE) {
        plant.ripeMs = 0;
        notice(
            plant.ownerKey,
            'ripe',
            'Your tobacco is ripe. Hold G at it to pick the leaves before they spoil.',
            'good',
        );
    } else {
        plant.dry = true;
        plant.dryMs = 0;
        plant.pestAt = rollPest(plant.stage);
    }
}

/** Grows the field by `dt`; `onRain` hears when rain starts on dry plants. */
export function grow(dt: number, onRain: () => void): void {
    const raining = isRaining();

    if (raining && !wasRaining && [...field.values()].some((entry) => entry.dry)) {
        onRain();
    }

    wasRaining = raining;

    for (const entry of [...field.values()]) {
        growPlant(entry, dt, raining);

        if (entry.quality <= 0) {
            remove(entry);
            notice(entry.ownerKey, 'withered', 'One of your tobacco plants withered.', 'bad');
        }
    }

    if (field.size) {
        growing();
    }
}

/** The quality a picked plant's leaves have. */
export function grade(entry: Plant): number {
    return Math.max(1, Math.round(entry.quality));
}
