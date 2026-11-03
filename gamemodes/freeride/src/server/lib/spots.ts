// Places a player comes up to: the ones each map has (worked out once per
// map and kept), the nearest one in reach, and arrows that fire once when
// walked onto.
import * as maps from './maps';
import { PlayerMap } from './players';
import { distance, type Point } from './util';

/**
 * What a map has (its stashes, its speed traps, its homes), built the first
 * time the map asks and kept: tick handlers ask many times a second.
 * Outside Free Ride it is `none`.
 */
export function perMap<T>(build: (map: maps.MapDefinition) => T, none: T): () => T {
    const built = new Map<maps.MapId, T>();

    return () => {
        const map = maps.current();

        if (!map) {
            return none;
        }

        if (!built.has(map.id)) {
            built.set(map.id, build(map));
        }

        return built.get(map.id) as T;
    };
}

/** The nearest of `items` within `reach` of `from`; the first of equals. */
export function nearest<T>(
    items: Iterable<T>,
    from: Point,
    reach: number,
    at: (item: T) => Point,
    measure: (a: Point, b: Point) => number = distance,
): T | null {
    let best: { item: T; gap: number } | null = null;

    for (const item of items) {
        const gap = measure(from, at(item));

        if (gap <= reach && (!best || gap < best.gap)) {
            best = { item, gap };
        }
    }

    return best?.item ?? null;
}

/**
 * A spot that does something when a player walks onto it (an entrance, a
 * way out). It fires once, and again only after the player has gone
 * `rearm` metres away, so standing on it does not fire it over and over;
 * a player who arrives on it (spawned there, or just moved there) has to
 * step off first. `key` tells apart the spots one player may walk onto.
 */
export class WalkIn {
    private readonly armed = new PlayerMap<Set<string>>();

    constructor(
        private readonly radius: number,
        private readonly rearm: number,
    ) {}

    /** Whether the player has just walked onto the spot. */
    reached(player: Player, spot: Point, key = ''): boolean {
        const gap = distance(player.position, spot);

        if (gap > this.rearm) {
            this.armedFor(player).add(key);

            return false;
        }

        if (gap <= this.radius && this.armed.get(player.id)?.has(key)) {
            this.disarm(player, key);

            return true;
        }

        return false;
    }

    /** The player has to step off the spot before it fires again. */
    disarm(player: Player, key = ''): void {
        this.armed.get(player.id)?.delete(key);
    }

    private armedFor(player: Player): Set<string> {
        let keys = this.armed.get(player.id);

        if (!keys) {
            keys = new Set();
            this.armed.set(player.id, keys);
        }

        return keys;
    }
}
