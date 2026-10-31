// The maps Free Ride plays: stock Lost Heaven, Lost Heaven Extended (the
// Freeride Extended mod as mission "freeride_extended", city and countryside
// in one map) and the stock countryside. Everything a map needs that cannot
// be read at runtime lives here: its road graph, its spawn districts and the
// landmarks races, jobs and events use. Positions come from the stock a1.dta
// scenes and the mod's scene2.bin; road graphs from tools/export_road_graph.py.
import * as barRoom from './bar_room';
import roadsFreeride from '../data/roads_freeride.js';
import roadsFreerideExtended from '../data/roads_freeride_extended.js';
import roadsFreekrajina from '../data/roads_freekrajina.js';
import type { RoadGraph } from '../data/types';
import type { Placement, Point } from './util';

export const DEFAULT_MAP = process.env.FREERIDE_MAP || 'freeride_extended';

/** A named place on a map. */
export interface Landmark {
    name: string;
    position: Point;
}

const LANDMARKS = {
    salieri: { name: "Salieri's bar", position: { x: -1774.5, y: -4.5, z: 5.2 } },
    pete: { name: "Yellow Pete's", position: { x: 61.1, y: 6.1, z: 113.5 } },
    pump: { name: 'the Night Pump', position: { x: -110.6, y: 8.5, z: -135.5 } },
    dispensary: { name: 'the Dispensary', position: { x: -761.3, y: 14.8, z: 762.5 } },
    // Freeride Extended: the crossing outside the "sector moto2" garage whose
    // props carry Bertone's name, and the crossing at the container docks.
    bertone: { name: "Lucas Bertone's garage", position: { x: -717.7, y: 1.4, z: 61.2 } },
    docks: { name: 'the container docks', position: { x: -2568.4, y: -5.9, z: -642.9 } },
    hospital: { name: 'Lost Heaven Hospital', position: { x: -759.4, y: 14.9, z: 759.5 } },
    // The stock FREEKRAJINA emeth_* frames; Freeride Extended carries the
    // same countryside at the same heights.
    crossroads: {
        name: 'the country crossroads',
        position: { x: -1485.96, y: 11.39, z: 939.36 },
    },
    lowlands: { name: 'the lowlands', position: { x: -1895.82, y: -4.53, z: 1886.73 } },
    hills: { name: 'the hills', position: { x: 363.97, y: 84.11, z: 2733.72 } },
    ridge: { name: 'the ridge', position: { x: 527.29, y: 32.13, z: 2054.58 } },
} satisfies Record<string, Landmark>;

export type LandmarkKey = keyof typeof LANDMARKS;

/** A race track built into a map: its starting grid and the racing line. */
export interface Circuit {
    /** The heading of the grid. */
    heading: number;
    grid: Point[];
    /** The lap's checkpoints in order; the last is the start/finish line. */
    lap: Point[];
}

// The Freeride Extended autodrom is the stock MISE06-AUTODROM track model,
// placed by its "anchr" frame at (-3736.52, 10.52, -1005.79) with rotation
// (0.1607, 0, -0.9870, 0) (models/autodrom.4ds). These are the stock race
// mission's start0-start15 grid frames and the 26 trackside nodes of its
// check.bin (type 8, at the race_cam* frames, linked into the racing line) in
// lap order, transformed by that anchor as S_quat::RotationMatrix does; the
// last point is the start/finish line just ahead of the grid.
const AUTODROM: Circuit = {
    heading: -1.8937,
    grid: [
        { x: -4070.17, y: 10.52, z: -972.35 },
        { x: -4065.62, y: 10.52, z: -958.79 },
        { x: -4053.89, y: 10.52, z: -966.95 },
        { x: -4049.55, y: 10.52, z: -953.58 },
        { x: -4037.65, y: 10.52, z: -962.05 },
        { x: -4033.18, y: 10.52, z: -948.61 },
        { x: -4021.44, y: 10.52, z: -956.91 },
        { x: -4016.87, y: 10.52, z: -943.6 },
        { x: -4004.66, y: 10.52, z: -951.61 },
        { x: -4000.36, y: 10.52, z: -938.09 },
        { x: -3988.58, y: 10.52, z: -946.22 },
        { x: -3984.11, y: 10.52, z: -932.75 },
        { x: -3972.24, y: 10.52, z: -941.38 },
        { x: -3967.83, y: 10.52, z: -927.95 },
        { x: -3956.05, y: 10.52, z: -936.24 },
        { x: -3951.65, y: 10.52, z: -922.81 },
    ],
    lap: [
        { x: -4124.63, y: 14.84, z: -969.05 },
        { x: -4216.84, y: 12.14, z: -1031.39 },
        { x: -4312.89, y: 13.67, z: -1103.79 },
        { x: -4341.42, y: 14.02, z: -1176.37 },
        { x: -4318.33, y: 19.04, z: -1246.22 },
        { x: -4290.85, y: 13.98, z: -1128.88 },
        { x: -4217.83, y: 11.76, z: -1072.88 },
        { x: -4145.15, y: 12.67, z: -1085.31 },
        { x: -4038.05, y: 13.96, z: -1160.5 },
        { x: -3923.67, y: 26.43, z: -1268.18 },
        { x: -4033.4, y: 26.31, z: -1371.33 },
        { x: -4041.21, y: 25.86, z: -1452.87 },
        { x: -3881.97, y: 42.58, z: -1564.21 },
        { x: -3773.44, y: 49.6, z: -1594.09 },
        { x: -3658.36, y: 31.43, z: -1501.55 },
        { x: -3582.29, y: 14.22, z: -1380.02 },
        { x: -3486.92, y: 13.29, z: -1268.12 },
        { x: -3459.18, y: 13.41, z: -1170.4 },
        { x: -3539.67, y: 13.41, z: -1123.19 },
        { x: -3615.56, y: 15.21, z: -1058.74 },
        { x: -3605.19, y: 16.62, z: -945.7 },
        { x: -3495.93, y: 25.03, z: -869.05 },
        { x: -3526.05, y: 17.82, z: -803.31 },
        { x: -3579.24, y: 30.69, z: -752.58 },
        { x: -3653.02, y: 14.35, z: -821.35 },
        { x: -3781.95, y: 13.12, z: -890.66 },
        { x: -4085.36, y: 10.52, z: -971.72 },
    ],
};

interface DistrictInfo {
    id: string;
    name: string;
    subtitle: string;
}

/**
 * A spawn district. One with a frame is placed where the client finds that
 * frame in the loaded scene; one without stands at its own position.
 */
export type District = DistrictInfo &
    (
        | { frame: string; position?: Point; direction?: Point }
        | { frame?: undefined; position: Point; direction: Point }
    );

/** A district placed by a frame of the stock countryside, which also knows where it is. */
type CountryDistrict = DistrictInfo & { frame: string; position: Point; direction: Point };

// Facing in degrees from each frame's stored rotation.
function country(
    id: string,
    name: string,
    subtitle: string,
    key: LandmarkKey,
    frame: string,
    degrees: number,
): CountryDistrict {
    const radians = (degrees * Math.PI) / 180;

    return {
        id,
        name,
        subtitle,
        frame,
        position: LANDMARKS[key].position,
        direction: { x: Math.sin(radians), y: 0, z: Math.cos(radians) },
    };
}

const CITY_DISTRICTS: District[] = [
    {
        id: 'little-italy',
        frame: 'emeth_1',
        name: 'Little Italy',
        subtitle: "A short walk from Salieri's bar",
    },
    { id: 'downtown', frame: 'emeth_3', name: 'Downtown', subtitle: 'Where the deals are made' },
    { id: 'hoboken', frame: 'emeth_4', name: 'Hoboken', subtitle: 'The rough side of town' },
];
const COUNTRY_DISTRICTS: CountryDistrict[] = [
    country(
        'crossroads',
        'Country Crossroads',
        'Where the city road meets the fields',
        'crossroads',
        'emeth_1',
        17,
    ),
    country('lowlands', 'The Lowlands', 'Flat farmland by the water', 'lowlands', 'emeth_2', 63),
    country('hills', 'The Hills', 'High ground, far from the law', 'hills', 'emeth_3', 123),
    country('ridge', 'The Ridge', 'Between the farms and the forest', 'ridge', 'emeth_4', 29),
];

/** A street race a map offers: from landmark to landmark along the roads. */
export interface RouteSpec {
    id: string;
    name: string;
    description: string;
    via: LandmarkKey[];
}

const CITY_ROUTES: RouteSpec[] = [
    {
        id: 'pump-loop',
        name: 'Pump Loop',
        description: "A quick blast from Pete's to the Night Pump and back.",
        via: ['pete', 'pump', 'pete'],
    },
    {
        id: 'dispensary-dash',
        name: 'Dispensary Dash',
        description: "Yellow Pete's to the Dispensary.",
        via: ['pete', 'dispensary'],
    },
    {
        id: 'night-pump-run',
        name: 'Night Pump Run',
        description: 'The Dispensary down to the Night Pump.',
        via: ['dispensary', 'pump'],
    },
    {
        id: 'little-italy-sprint',
        name: 'Little Italy Sprint',
        description: "Salieri's bar across the river to Yellow Pete's.",
        via: ['salieri', 'pete'],
    },
    {
        id: 'grand-tour',
        name: 'Grand Tour',
        description: "Salieri's, the Dispensary, the Night Pump and home again.",
        via: ['salieri', 'dispensary', 'pump', 'salieri'],
    },
];
const EXTENDED_ROUTES: RouteSpec[] = [
    {
        id: 'harbour-dash',
        name: 'Harbour Dash',
        description: "Salieri's bar to the container docks.",
        via: ['salieri', 'docks'],
    },
    {
        id: 'bertone-run',
        name: 'Bertone Run',
        description: "Yellow Pete's to Lucas Bertone's garage.",
        via: ['pete', 'bertone'],
    },
    {
        id: 'country-mile',
        name: 'Country Mile',
        description: 'Out of the city to the country crossroads.',
        via: ['dispensary', 'crossroads'],
    },
    {
        id: 'hill-climb',
        name: 'Hill Climb',
        description: 'The lowlands up to the hills.',
        via: ['lowlands', 'hills'],
    },
    {
        id: 'lost-heaven-gp',
        name: 'Lost Heaven Grand Prix',
        description: "City to countryside and back: Pete's, the ridge, the hills and Pete's again.",
        via: ['pete', 'ridge', 'hills', 'pete'],
    },
];
const COUNTRY_ROUTES: RouteSpec[] = [
    {
        id: 'country-loop',
        name: 'Country Loop',
        description: 'Crossroads, ridge, hills and back.',
        via: ['crossroads', 'ridge', 'hills', 'crossroads'],
    },
    {
        id: 'hill-climb',
        name: 'Hill Climb',
        description: 'The lowlands up to the hills.',
        via: ['lowlands', 'hills'],
    },
    {
        id: 'farm-sprint',
        name: 'Farm Sprint',
        description: 'The crossroads to the lowlands.',
        via: ['crossroads', 'lowlands'],
    },
];

/** The county jail's ways in and out on a map. */
export interface JailExits {
    /** Where prisoners and visitors come out. */
    release: Placement;
    /** The visitors' entrance; none in the countryside. */
    visit?: { position: Point };
}

// The county jail (lib/prison.ts) in the city: its way out leads to the
// street outside the gate of the stock prison building, models/vezeni.4ds,
// which cache.bin places at (-1105.68, -6.12, 311.77) turned a quarter about
// y. Its gate (texture 0VRATA1S) spans x -1102.31..-1094.44 on the south
// wall at z 296.14, ground y -6.12, facing the road at z 284. The visitors'
// entrance is the arrow outside that gate; prisoners and visitors come out
// 5 m into the street, facing the road. In the countryside the way out
// leads to the crossroads and there is no visitors' entrance.
const CITY_JAIL: JailExits = {
    release: { position: { x: -1098.4, y: -5.0, z: 291.0 }, heading: Math.PI },
    visit: { position: { x: -1098.4, y: -6.0, z: 294.6 } },
};

/** A building a map lets players walk into (server/interiors.ts). */
export interface MapInterior {
    id: string;
    name: string;
    kind: 'public' | 'safehouse';
    /** The virtual world of an interior built apart from the street. */
    world?: number;
    /** Whether a room built from map objects stands yet. */
    open?: () => boolean;
    outside: Placement;
    inside: Placement;
}

export interface Hospital {
    name: string;
    door: Point;
    spawn: Placement;
}

export type MapId = 'freeride' | 'freeride_extended' | 'freekrajina';

export interface MapDefinition {
    id: MapId;
    title: string;
    roads: RoadGraph;
    /** Whether the stock shops stand on it. */
    shops: boolean;
    /** The positions of the scene frames the districts are placed by. */
    frames: Record<string, Point>;
    districts: District[];
    routes: RouteSpec[];
    places: LandmarkKey[];
    exportYard: Landmark;
    jail: JailExits;
    interiors?: MapInterior[];
    circuit?: Circuit;
    hospital?: Hospital;
}

export const MAPS: Readonly<Record<MapId, MapDefinition>> = {
    freeride: {
        id: 'freeride',
        title: 'Lost Heaven',
        roads: roadsFreeride,
        shops: true,
        frames: {
            emeth_1: { x: -1984.884, y: -5.032, z: 23.145 },
            emeth_3: { x: -189.258, y: 18.106, z: -690.477 },
            emeth_4: { x: 529.439, y: -5.477, z: 86.797 },
        },
        districts: CITY_DISTRICTS,
        routes: CITY_ROUTES,
        places: ['salieri', 'pete', 'pump', 'dispensary'],
        exportYard: { name: 'the export yard', position: { x: -1141.0, y: -7.1, z: -709.9 } },
        jail: CITY_JAIL,
        // Salieri's door (the stock "Salieri_save" frame, -1774.52, 5.25)
        // leads into the back-room bar built from map objects
        // (lib/bar_room.ts); the stock scene has no rooms behind it.
        interiors: [
            {
                id: 'b:salieri-bar',
                name: "Salieri's Bar",
                kind: 'public',
                world: barRoom.WORLD,
                open: barRoom.isBuilt,
                outside: { position: { x: -1776.3, y: -5.5, z: 3.9 }, heading: Math.PI },
                inside: barRoom.arrival(),
            },
        ],
    },
    freeride_extended: {
        id: 'freeride_extended',
        title: 'Lost Heaven Extended',
        roads: roadsFreerideExtended,
        shops: true,
        frames: {
            emeth_1: { x: -1984.8, y: -5.03, z: 23.14 },
            emeth_3: { x: -189.26, y: 18.11, z: -690.48 },
            emeth_4: { x: 529.44, y: -5.48, z: 86.8 },
        },
        // The countryside districts have no frames in this scene; the server
        // places them from the stock countryside's own spawn frames.
        districts: [
            ...CITY_DISTRICTS,
            ...COUNTRY_DISTRICTS.map(({ frame: _frame, ...district }) => district),
        ],
        routes: [...CITY_ROUTES, ...EXTENDED_ROUTES],
        places: [
            'salieri',
            'pete',
            'pump',
            'dispensary',
            'bertone',
            'docks',
            'hospital',
            'crossroads',
            'lowlands',
            'hills',
            'ridge',
        ],
        exportYard: LANDMARKS.bertone,
        circuit: AUTODROM,
        // models/9medic.4ds placed by cache.bin at (-759.22, 11.56, 778.99),
        // unrotated; its doors sit at local z -15.5, beside the stock
        // "lekarna" frame where the Dispensary's attendant stands (y 14.81).
        hospital: {
            name: 'Lost Heaven Hospital',
            door: { x: -759.4, y: 14.9, z: 761.5 },
            spawn: { position: { x: -759.4, y: 14.9, z: 759.5 }, heading: Math.PI },
        },
        jail: CITY_JAIL,
        interiors: [
            // Sarah's apartment, models/quarters_sr.4ds: the street door frame
            // "&& futro out" and the corridor "&& chodba" behind it.
            {
                id: 'b:sarah',
                name: "Sarah's Apartment",
                kind: 'safehouse',
                outside: { position: { x: -1770.6, y: -5.2, z: 171.5 }, heading: -1.54 },
                inside: { position: { x: -1759.4, y: -3.3, z: 171.8 }, heading: 1.6 },
            },
            // Morello's bar, models/morellobar.4ds: its room ("Sect podl", floor
            // y 17.31) behind the shop windows on the south front (z 451.5),
            // whose bins stand on the pavement at z 450. Its door off the yard
            // is a fixed mesh, so the arrow on the pavement leads in. Salieri's
            // bar (models/salierybar.4ds) has working doors and needs no arrow.
            {
                id: 'b:morello',
                name: "Morello's Bar",
                kind: 'public',
                outside: { position: { x: -411.5, y: 17.45, z: 449.9 }, heading: Math.PI },
                inside: { position: { x: -414.7, y: 17.36, z: 454.2 }, heading: Math.PI / 2 },
            },
        ],
    },
    freekrajina: {
        id: 'freekrajina',
        title: 'The Countryside',
        roads: roadsFreekrajina,
        shops: false,
        frames: Object.fromEntries(
            COUNTRY_DISTRICTS.map((district) => [district.frame, district.position]),
        ),
        districts: COUNTRY_DISTRICTS,
        routes: COUNTRY_ROUTES,
        places: ['crossroads', 'lowlands', 'hills', 'ridge'],
        exportYard: LANDMARKS.lowlands,
        jail: {
            release: { position: LANDMARKS.crossroads.position, heading: (17 * Math.PI) / 180 },
        },
    },
};
// The stock night map plays only until Free Ride moves it to the default.
export const NIGHT_MAP = 'freeridenoc';

function isMapId(id: string): id is MapId {
    return Object.hasOwn(MAPS, id);
}

/** The map a mission plays, or null for a mission that is not Free Ride's. */
export function get(id: string): MapDefinition | null {
    if (isMapId(id)) {
        return MAPS[id];
    }

    return id === NIGHT_MAP ? MAPS.freeride : null;
}

export function current(): MapDefinition | null {
    return get(World.getMission());
}

export function landmark(key: LandmarkKey): Landmark {
    return LANDMARKS[key];
}

/** A map's landmarks, with their keys. */
export function places(map = current()): (Landmark & { key: LandmarkKey })[] {
    return (map?.places ?? []).map((key) => ({ key, ...LANDMARKS[key] }));
}

export function ids(): MapId[] {
    return Object.keys(MAPS).filter(isMapId);
}
