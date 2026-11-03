// Where the farm is. Each map has its own site; both have seven rows of eight
// plots, so a plant keeps its plot (row and place in the row) from map to
// map, and a plant saved at the old Lost Heaven Extended field grows on in
// the same plot of the new one. Ground heights are sampled from each map's
// ground mesh; every spot below was checked clear of the scene's objects.
//
// The Countryside (FREEKRAJINA): the stock models/4stodola.4ds its scene2.bin
// places at (-2295.25, -3.62, 711.61), rotation (0.447, 0, 0.894, 0), at the
// end of the lowland road: road graph crossing (-2306.2, -5.9, 668.8) is a
// dead end 32 m from the barn, with fences (@PLplot) only on the far side of
// the road. Applied as LS3D does (the conjugate, see lib/maps.ts) its lean-to
// side, local -x, faces (0.6, -0.8), toward the field: open grass east of the
// barn. Heights from the FREEKRAJINA scene.4ds ground mesh.
//
// Lost Heaven Extended: the same barn is no farm there. The mod's scene.4ds
// adds "Loft14", a 3 m plank wall (0LKULNA1.BMP) from (-2172.8, 496.4) to
// (-2349.1, 781.2) with 16 collision faces and two plates in its tree.klz,
// between that road and the barn: the trucks and the loading bay were on the
// road side, the arrow and the field behind the wall. The farm is the mod's
// farmstead in the north of the countryside instead: scene2.bin's "prejimka"
// (models/prejimka.4ds, its rebuild of the stock FREEKRAJINA "prejimka*"
// frames) furnished with straw, sacks, carts and tractor "traktor1". The way
// in is its bridge ("2obj80"/"Object08", deck y 19.8, 6 m between the rails)
// over the creek, 22 m north of the road graph line at (-840, 18.6, 3316),
// on the main network (4.4 km by road to the village co-op, 5.8 km to the
// docks), then the lane north (Loft05/Object25, fenced on the east by
// Object31/28/26) into the yard. The barn ("&& stodola1-4", floor y 19.86)
// opens to the west between its posts at (-849.6, 3491) and (-851.8, 3497.8);
// the arrow is in front of that door. West of it lies the fenced paddock
// (Object14/Cylinder134 north, Object13/Object11 west, Object32 south, with
// collision kolize5/6/8) whose east side is open between Object09 (ends at
// (-863.2, 3505.7)) and Object07 (ends at (-858.8, 3491.5)): the field fills
// it on flat grass (y 19.45 to 19.79), the trucks wait beside it facing the
// gap and the loading bay is in the gap, clear of the crates there
// ("anchr" boxes x -864.7 to -862.8, z 3496.6 to 3499.7).
import * as maps from '../lib/maps';
import { COOP, DOCKS, type Buyer } from './market';
import type { Placement, Point } from '../lib/util';

/** A plot of a field. Its id is its row and place, the same on every site. */
export interface Plot extends Point {
    id: number;
    row: number;
}

export interface Site {
    /** Where to sign on, as the directions name it. */
    place: string;
    signOn: Point;
    /** Where the farm trucks wait. */
    spots: Placement[];
    loading: Point;
    plots: Plot[];
    field: { minX: number; maxX: number; minZ: number; maxZ: number; center: Point };
    bales: Point[];
}

interface Farm {
    site: Site;
    buyers: Buyer[];
}

// The plots of a field: seven rows dz apart, eight plots dx apart in each,
// with the ground height of each plot.
function plots(x0: number, dx: number, z0: number, dz: number, ground: number[][]): Plot[] {
    return ground.flatMap((row, r) =>
        row.map((y, c) => ({
            id: r * row.length + c,
            row: r + 1,
            x: x0 + c * dx,
            y,
            z: z0 + r * dz,
        })),
    );
}

export const PLOT_COUNT = 56;
/** The farm's colour: its arrow, its trucks, its markers and feed lines. */
export const LEAF = '#a7b85a';

const LOWLANDS: Site = {
    place: 'the barn in the lowlands',
    signOn: { x: -2286.7, y: -5.0, z: 700.2 },
    // On the flat apron between the road and the barn, noses north-west
    // along the road; ground -4.68 and -4.53 under both ends.
    spots: [
        { position: { x: -2314.5, y: -4.63, z: 716.0 }, heading: -0.675 },
        { position: { x: -2311.2, y: -4.48, z: 718.6 }, heading: -0.675 },
    ],
    // In front of the barn's road side, 7 m out.
    loading: { x: -2305.0, y: -4.84, z: 702.0 },
    plots: plots(-2276, 3, 699, 5, [
        [-5.52, -5.64, -5.75, -5.87, -5.98, -6.09, -6.21, -6.32],
        [-5.1, -5.22, -5.33, -5.45, -5.56, -5.68, -5.79, -5.9],
        [-4.68, -4.8, -4.91, -5.03, -5.14, -5.26, -5.37, -5.49],
        [-4.26, -4.38, -4.49, -4.61, -4.72, -4.84, -4.95, -5.07],
        [-3.85, -3.96, -4.07, -4.19, -4.3, -4.42, -4.53, -4.65],
        [-3.5, -3.54, -3.66, -3.77, -3.88, -4.0, -4.11, -4.23],
        [-3.34, -3.28, -3.24, -3.35, -3.47, -3.58, -3.69, -3.81],
    ]),
    field: {
        minX: -2277.5,
        maxX: -2253.5,
        minZ: 697,
        maxZ: 731,
        center: { x: -2265.5, y: -4.6, z: 714 },
    },
    // Hay bales at the field's corners mark it out.
    bales: [
        { x: -2279.5, y: -5.87, z: 695.0 },
        { x: -2252.5, y: -6.9, z: 695.0 },
        { x: -2279.5, y: -3.45, z: 733.5 },
        { x: -2252.5, y: -3.68, z: 733.5 },
    ],
};

// Heights from the prejimka model's own ground patches (Object21, Object23,
// Object27, Object30), which lie over the mod's countryside mesh scene_fk.
const FARMSTEAD: Site = {
    place: 'the barn of the farmstead north-west of the hills, over the creek bridge',
    // Outside the barn's west door, 3.5 m from the barrels at its north post.
    signOn: { x: -854.0, y: 19.88, z: 3494.5 },
    // In the paddock between the field and the crates, noses north towards
    // the gap; ground 19.51/19.57 and 19.67/19.71 under the ends.
    spots: [
        { position: { x: -871.0, y: 19.54, z: 3490.0 }, heading: 0 },
        { position: { x: -866.5, y: 19.69, z: 3490.0 }, heading: 0 },
    ],
    // In the paddock's gap, between the barn and the field.
    loading: { x: -859.5, y: 20.12, z: 3499.0 },
    plots: plots(-897.5, 3, 3478, 5, [
        [19.45, 19.49, 19.57, 19.6, 19.66, 19.73, 19.79, 19.76],
        [19.46, 19.46, 19.46, 19.48, 19.51, 19.54, 19.56, 19.58],
        [19.48, 19.48, 19.48, 19.47, 19.49, 19.51, 19.52, 19.5],
        [19.49, 19.48, 19.48, 19.48, 19.48, 19.49, 19.5, 19.51],
        [19.48, 19.48, 19.47, 19.47, 19.48, 19.49, 19.5, 19.5],
        [19.48, 19.47, 19.47, 19.47, 19.48, 19.49, 19.5, 19.53],
        [19.47, 19.47, 19.47, 19.47, 19.49, 19.5, 19.51, 19.57],
    ]),
    field: {
        minX: -899,
        maxX: -875,
        minZ: 3476,
        maxZ: 3510,
        center: { x: -887, y: 19.48, z: 3493 },
    },
    // Bales at the field's corners, each 1.5 m or more clear of the paddock
    // fences and the stable (Object119, south of the field).
    bales: [
        { x: -898.5, y: 19.57, z: 3473.0 },
        { x: -873.0, y: 19.85, z: 3475.0 },
        { x: -901.5, y: 19.47, z: 3508.0 },
        { x: -873.5, y: 19.68, z: 3510.0 },
    ],
};

// The farmstead is 4.4 km from the co-op and 5.8 km from the docks by road,
// where the old Lost Heaven Extended barn was 1.3 and 1.7 km away. The
// longer drive pays a little more, but a full load stays near the other
// jobs' biggest calls for its 15-20 minutes: about $970 at the co-op and
// $1,320 at the docks, whose run is the risky one.
const FARMS: Partial<Record<maps.MapId, Farm>> = {
    freekrajina: { site: LOWLANDS, buyers: [COOP] },
    freeride_extended: {
        site: FARMSTEAD,
        buyers: [
            { ...COOP, rate: 1.1 },
            { ...DOCKS, rate: 1.5 },
        ],
    },
};

for (const farm of Object.values(FARMS)) {
    if (farm.site.plots.length !== PLOT_COUNT) {
        throw new Error('farm: every site needs the same plots');
    }
}

/** Whether this server can show the plants and run the hoses. */
export function available(): boolean {
    return typeof MapObject !== 'undefined' && typeof WaterJet !== 'undefined';
}

function farm(): Farm | null {
    const map = maps.current();

    return map && available() ? (FARMS[map.id] ?? null) : null;
}

/** Whether the loaded map has the farm (and the server can run it). */
export function here(): boolean {
    return farm() !== null;
}

/** The current map's farm site; null on a map without the farm. */
export function site(): Site | null {
    return farm()?.site ?? null;
}

export function buyers(): Buyer[] {
    return farm()?.buyers ?? [];
}

/** The site, where only a map with the farm gets: the clock and the work on it. */
export function siteNow(): Site {
    const current = site();

    if (!current) {
        throw new Error('farm: no farm on this map');
    }

    return current;
}
