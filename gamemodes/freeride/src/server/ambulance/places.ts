// Lost Heaven Hospital: the stock models/9medic.4ds, which both city scenes'
// cache.bin place at (-759.22, 11.56, 778.99). Its entrance steps run down
// to z 758 facing the street whose road graph line runs along z 729.1; the
// sign-on arrow is at the foot of the steps.
import type { StationPlace } from '../lib/stations';
import type { Point } from '../lib/util';

export const AMBULANCE = 'ambulance00.i3d';

export interface HospitalPlace extends StationPlace {
    name: string;
    /** Where the patients are unloaded. */
    bay: Point;
    /** Where they walk in. */
    door: Point;
}

const CITY_HOSPITAL: HospitalPlace = {
    name: 'Lost Heaven Hospital',
    signOn: { x: -759.4, y: 11.6, z: 755.5 },
    // Off the road, on the forecourt between the kerb (z 739.5, 0.2 m up from
    // the street) and the steps, either side of the walk to the doors, noses
    // to +x. The street in front is a T-junction (road crossing at x -759.7,
    // z 729.1): anything stood in it holds up the traffic. The forecourt is
    // level at 11.35-11.4 under both cars (Line03aaa of scene.4ds) and a
    // parked car nobody drives does not settle, so they stand at that height.
    spots: [
        { position: { x: -749.0, y: 11.45, z: 746.5 }, heading: Math.PI / 2 },
        { position: { x: -770.0, y: 11.45, z: 746.5 }, heading: Math.PI / 2 },
    ],
    // On the forecourt too, not in the junction.
    bay: { x: -759.4, y: 11.4, z: 745.0 },
    // The landing in front of the doors (3DVERE4 at z 763.5, floor y 13.2).
    door: { x: -759.4, y: 13.3, z: 762.0 },
};
const HOSPITALS: Readonly<Record<string, HospitalPlace>> = {
    freeride: CITY_HOSPITAL,
    freeride_extended: CITY_HOSPITAL,
    freeridenoc: CITY_HOSPITAL,
};

/** The hospital of the loaded mission, if it has one. */
export function hospital(): HospitalPlace | null {
    return HOSPITALS[World.getMission()] ?? null;
}
