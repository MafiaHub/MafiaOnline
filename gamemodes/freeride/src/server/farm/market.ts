// Who buys the cured tobacco, and for how much. The village buyer is the main
// street crossing in front of the village store (models/4build76.4ds at
// (-1488.4, 14.4, 714.4) in FREEKRAJINA, (-1489.8, 14.5, 714.6) in the mod's
// cache.bin); the road graph crossing (-1484.1, 14.6, 725.1) is on both maps.
// The docks are the mod's container docks crossing (lib/maps.ts).
import * as maps from '../lib/maps';
import type { Point } from '../lib/util';
import type { Bundles } from './state';

const BASE_PRICE = 110;

export interface Buyer {
    id: string;
    name: string;
    label: string;
    mapLabel: string;
    position: Point;
    /** The price of a bundle, as a share of BASE_PRICE. */
    rate: number;
    /** Near it the whole city hears of a load, and anyone may take the truck. */
    risky?: boolean;
}

export const COOP: Buyer = {
    id: 'coop',
    name: 'the village co-op',
    label: 'TOBACCO BUYER',
    mapLabel: 'Tobacco buyer',
    position: { x: -1484.1, y: 14.6, z: 725.1 },
    rate: 1,
};

export const DOCKS: Buyer = {
    id: 'docks',
    name: 'the container docks',
    label: 'TOBACCO EXPORT',
    mapLabel: 'Tobacco export',
    position: maps.landmark('docks').position,
    rate: 1.7,
    risky: true,
};

/** The average quality of the bundles, in percent. */
export function quality(entry: Bundles): number {
    return entry.n ? Math.round(entry.q / entry.n) : 0;
}

/** What one bundle of this quality fetches at the buyer. */
export function priceOf(entry: Bundles, buyer: Buyer): number {
    return Math.round(BASE_PRICE * buyer.rate * (0.35 + (0.65 * quality(entry)) / 100));
}
