// Shapes of the generated data modules in this directory (CommonJS, written
// by the tools/ exporters; see each file's header for how it was made).

/** [x, y, z] */
export type Triple = [number, number, number];

export interface RoadGraph {
    crossings: Triple[];
    /** [from crossing, to crossing, length, road points from -> to] */
    edges: [number, number, number, Triple[]][];
}

export interface Skyline {
    cell: number;
    originX: number;
    originZ: number;
    cols: number;
    rows: number;
    /** base64 of zlib-deflated little-endian int16 heights, row by row. */
    heights: string;
}

export type HouseTier = 'poor' | 'middle' | 'rich';

/** [id, tier, model, door x, y, z, heading out of the door, parking x, y, z, heading] (parking optional). */
export type HouseRow =
    | [string, HouseTier, string, number, number, number, number]
    | [string, HouseTier, string, number, number, number, number, number, number, number, number];

/** [frame name, x, y, z] */
export type BoothRow = [string, number, number, number];
