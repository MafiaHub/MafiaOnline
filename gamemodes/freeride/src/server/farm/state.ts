// What the farm holds: the plants on the field, each grower's leaves, curing
// batches and cured tobacco, and the loads on trucks. Plants live on the
// server's farm clock: it runs while a map with the farm is loaded, so a crop
// waits through a map without it or a restart, but not through its owner
// leaving. Plants, leaves, curing batches and cured tobacco are kept in the
// database by player record; a load on a truck goes back to its owner's barn
// when the map changes or the resource stops.
import * as store from '../lib/store';
import { PlayerMap } from '../lib/players';
import { PLOT_COUNT } from './sites';

const STORE = 'farm';
const SAVE_MS = 15_000;
// Plants growing is all that changed: saved this often, the field document
// is not rewritten every SAVE_MS while anything grows.
const GROWTH_SAVE_MS = 60_000;

/** A plant on a plot, owned by a player record (and shared with its crew). */
export interface Plant {
    plot: number;
    ownerKey: string;
    ownerName: string;
    ownerCrew: string | null;
    /** 0 seedling, 1 young, 2 leafy, 3 ripe. */
    stage: number;
    /** Time grown in the current stage. */
    grown: number;
    dry: boolean;
    dryMs: number;
    pest: boolean;
    /** When in the stage's growth the hornworms come, if they do. */
    pestAt: number | null;
    ripeMs: number;
    quality: number;
    yaw: number;
}

/** Bundles of tobacco: how many and the sum of their quality. */
export interface Bundles {
    n: number;
    q: number;
}

export interface Batch extends Bundles {
    left: number;
}

/** What one grower has at the barn. */
export interface Stock {
    name: string;
    leaves: Bundles;
    curing: Batch[];
    cured: Bundles;
}

/** Tobacco on a farm truck, still its owner's until sold. */
export interface Load extends Bundles {
    ownerKey: string;
    ownerName: string;
    ownerCrew: string | null;
    /** The city has heard of it: it flashes on everyone's map. */
    rumoured: boolean;
}

/** A load going onto a truck at the bay, or a refusal already told. */
export type Loading =
    | { vehicleId: number; refused: true }
    | { vehicleId: number; refused?: false; startedAt: number };

interface Saved {
    plants?: unknown[];
    stocks?: Record<string, Partial<Stock>>;
}

// Plot id -> plant.
export const field = new Map<number, Plant>();
// Player record key -> stock.
export const stocks = new Map<string, Stock>();
// Vehicle id -> the tobacco on it.
export const cargo = new Map<number, Load>();
export const loading = new PlayerMap<Loading>({ clearOnMissionChange: true });

let dirty = false;
let grew = false;
let lastSaveAt = 0;

/** Something worth saving changed. */
export function changed(): void {
    dirty = true;
}

/** Plants grew: saved, but less often. */
export function growing(): void {
    grew = true;
}

export function stockOf(key: string, ownerName: string | null = null): Stock {
    let stock = stocks.get(key);

    if (!stock) {
        stock = {
            name: ownerName ?? '',
            leaves: { n: 0, q: 0 },
            curing: [],
            cured: { n: 0, q: 0 },
        };

        stocks.set(key, stock);
    }

    if (ownerName) {
        stock.name = ownerName;
    }

    return stock;
}

/** Puts a load back in its owner's barn. */
export function unload(entry: Load): void {
    const stock = stockOf(entry.ownerKey, entry.ownerName);

    stock.cured.n += entry.n;
    stock.cured.q += entry.q;
    dirty = true;
}

// Plots are kept by index, the same row and place on every site.
function savedPlant(raw: unknown): Plant | null {
    if (typeof raw !== 'object' || raw === null) {
        return null;
    }

    const plant = raw as Partial<Plant>;
    const plot = plant.plot;

    return typeof plot === 'number' &&
        Number.isInteger(plot) &&
        plot >= 0 &&
        plot < PLOT_COUNT &&
        typeof plant.ownerKey === 'string'
        ? (plant as Plant)
        : null;
}

export function load(): void {
    const saved = store.load<Saved>(STORE) ?? {};

    for (const raw of saved.plants ?? []) {
        const plant = savedPlant(raw);

        if (plant) {
            field.set(plant.plot, plant);
        }
    }

    for (const [key, stock] of Object.entries(saved.stocks ?? {})) {
        stocks.set(key, {
            name: stock.name ?? '',
            leaves: stock.leaves ?? { n: 0, q: 0 },
            curing: stock.curing ?? [],
            cured: stock.cured ?? { n: 0, q: 0 },
        });
    }
}

export function persist(): void {
    const kept = [...stocks].filter(
        ([, stock]) => stock.leaves.n || stock.curing.length || stock.cured.n,
    );

    store.save(STORE, { plants: [...field.values()], stocks: Object.fromEntries(kept) });
    dirty = false;
    grew = false;
    lastSaveAt = Date.now();
}

export function saveIfDue(now: number): void {
    if ((dirty && now - lastSaveAt >= SAVE_MS) || (grew && now - lastSaveAt >= GROWTH_SAVE_MS)) {
        persist();
    }
}
