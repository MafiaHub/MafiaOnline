// What the fire brigade answers. A call (an incident) is one or more fires,
// the people caught in them and, now and then, the arsonist who set it.
import * as interiors from '../interiors';
import * as rooms from '../lib/house_rooms';
import type { Bar } from '../bars';
import type { HouseTier } from '../data/types';
import { where } from '../lib/callouts';
import type { MapInterior } from '../lib/maps';
import { PlayerMap } from '../lib/players';
import { distance, flatDistance, type Placement, type Point } from '../lib/util';

export type IncidentKind = 'house' | 'car' | 'crash' | 'pump' | 'bar' | 'pileup' | 'building';

/**
 * spawning until the host made them, then trapped; following a firefighter
 * out; outside on a bar's pavement; safe, dead, or gone (the host lost them).
 */
export type CivilianState =
    'spawning' | 'trapped' | 'following' | 'outside' | 'safe' | 'dead' | 'gone';

/** Someone caught in a fire. */
export interface Civilian {
    /** Which of the call's people: the side of the crash, the bar's pavement spot. */
    index: number;
    npc: Npc | null;
    ready: boolean;
    clip: string;
    model: string;
    voice: number;
    state: CivilianState;
    leaderId: number | null;
    nextLineAt: number;
    /** The same person come out of a bar onto the pavement. */
    outside: Npc | null;
    outsideReady: boolean;
    /** The room of a burning building they are in. */
    room: rooms.RoomKey | null;
    /** Made again in the next room (building.ts): they follow their leader once the host made them. */
    relead: boolean;
}

export type ArsonistState = 'fleeing' | 'caught' | 'escaped' | 'dead';

export interface Arsonist {
    npc: Npc | null;
    state: ArsonistState;
    ready: boolean;
    /** The run he is on (npcTaskComplete starts the next). */
    task: number;
    until: number;
    /** The officers told about him, whose compass points at him. */
    alerted: Set<number>;
}

/** A bar on fire, people inside. */
export interface BarFire {
    id: Bar['id'];
    name: string;
    interior: MapInterior;
    /** The world the bar's room is in. */
    world: number;
    /** Where the people led out stand on the pavement. */
    street: readonly Point[];
}

/** Where a fire can burn in a building's room, room-local; `what` is what is alight. */
export interface FireSpot {
    room: rooms.RoomKey;
    x: number;
    z: number;
    radius: number;
    height: number;
    what: string;
}

/** A house on fire with people inside: the crew goes in (building.ts). */
export interface BuildingFire {
    houseId: string;
    name: string;
    tier: HouseTier;
    /** The world its rooms stand in for this call. */
    world: number;
    rooms: readonly rooms.RoomKey[];
    entry: rooms.RoomKey;
    door: Point;
    /** Where people come out of the door. */
    out: Placement;
    /** Where those led out stand on the pavement. */
    street: readonly Point[];
    /** The room each fire inside burns in, by fire id. */
    roomOf: Map<number, rooms.RoomKey>;
    /** Spots not alight yet, where it spreads to. */
    spare: FireSpot[];
    nextSpreadAt: number;
    /** When the smoke takes whoever is still trapped. */
    smokeAt: number;
    smokeWarned: boolean;
    /** What the caller told the dispatcher. */
    story: string;
}

/** A filling station on fire. */
export interface PumpFire {
    pump: Point;
    /** When the pumps go up; null once safe or gone up. */
    fuseAt: number | null;
    warned: boolean;
}

export interface Incident {
    id: number;
    kind: IncidentKind;
    position: Point;
    startedAt: number;
    /** The burning car, or a pile-up's wreck. */
    vehicleId: number | null;
    fire: Fire;
    /** The further fires: the pumps, the rooms of a bar, the fuel store once the pumps went up. */
    extra: Fire[];
    civilians: Civilian[];
    /** Water each firefighter put on it, by player id. */
    water: Map<number, number>;
    /** Who led someone out. */
    rescuers: Set<number>;
    generation: number;
    label: string;
    where: string;
    arsonist: Arsonist | null;
    bar: BarFire | null;
    building: BuildingFire | null;
    pumps: PumpFire | null;
    /** The car (or the pumps) went up. */
    blown: boolean;
    /** A pile-up's wreck stays: the ambulance clears it (ambulance/scenes.ts). */
    keepVehicle: boolean;
}

export const incidents = new Map<number, Incident>();
let nextIncident = 1;

export function newIncident(
    kind: IncidentKind,
    position: Point,
    label: string,
    fire: Fire,
    fields: Partial<
        Pick<Incident, 'vehicleId' | 'extra' | 'bar' | 'building' | 'pumps' | 'keepVehicle'>
    > = {},
): Incident {
    return {
        id: nextIncident++,
        kind,
        position,
        startedAt: Date.now(),
        vehicleId: null,
        fire,
        extra: [],
        civilians: [],
        water: new Map(),
        rescuers: new Set(),
        generation: World.getMissionGeneration(),
        label,
        where: where(position),
        arsonist: null,
        bar: null,
        building: null,
        pumps: null,
        blown: false,
        keepVehicle: false,
        ...fields,
    };
}

/** Every fire of a call: the main one and the further ones. */
export function firesOf(incident: Incident): Fire[] {
    return [incident.fire, ...incident.extra];
}

export function burning(incident: Incident): boolean {
    return firesOf(incident).some((fire) => fire.burning);
}

export function intensityOf(incident: Incident): number {
    return firesOf(incident).reduce(
        (highest, fire) => Math.max(highest, fire.burning ? fire.intensity : 0),
        0,
    );
}

/** What the call is, in a few words: "House fire", "Fire at Morello's Bar". */
export function titleOf(incident: Incident): string {
    if (incident.bar) {
        return `Fire at ${incident.bar.name}`;
    }

    if (incident.building) {
        return 'Building fire';
    }

    if (incident.kind === 'pump') {
        return 'Filling station fire';
    }

    if (incident.kind === 'pileup') {
        return 'Burning wreck';
    }

    return incident.label
        .split(' · ')[0]
        .toLowerCase()
        .replace(/^./, (first) => first.toUpperCase());
}

/** People still to get out: trapped, being led, or just come out of a bar. */
export function waiting(incident: Incident): boolean {
    return incident.civilians.some((civilian) =>
        ['trapped', 'spawning', 'following', 'outside'].includes(civilian.state),
    );
}

export function fleeing(incident: Incident): boolean {
    return incident.arsonist?.state === 'fleeing';
}

/** Still the brigade's: burning, someone trapped or the arsonist loose. */
export function active(incident: Incident): boolean {
    return (
        burning(incident) ||
        incident.civilians.some((civilian) => civilian.state === 'trapped') ||
        fleeing(incident)
    );
}

export function nearestIncident(position: Point): { incident: Incident; gap: number } | null {
    return (
        [...incidents.values()]
            .filter(active)
            .map((incident) => ({ incident, gap: flatDistance(incident.position, position) }))
            .sort((a, b) => a.gap - b.gap)[0] ?? null
    );
}

export function incidentOfFire(fire: Fire): Incident | null {
    for (const incident of incidents.values()) {
        if (firesOf(incident).some((entry) => entry.id === fire.id)) {
            return incident;
        }
    }

    return null;
}

/** Firefighters inside a burning building (building.ts): the call and the room. */
export const indoors = new PlayerMap<{ incidentId: number; room: rooms.RoomKey }>();

/** The burning building or bar whose rooms the player is in. */
export function burningInterior(player: Player): Incident | null {
    const spot = indoors.get(player.id);

    if (spot) {
        return incidents.get(spot.incidentId) ?? null;
    }

    const interior = interiors.insideOf(player);

    if (!interior) {
        return null;
    }

    return (
        [...incidents.values()].find((incident) => incident.bar?.interior.id === interior.id) ??
        null
    );
}

/**
 * The call nearest the player: the one whose rooms they are in (gap 0),
 * else the nearest along the street.
 */
export function incidentNear(player: Player): { incident: Incident; gap: number } | null {
    const inside = burningInterior(player);

    if (inside && active(inside)) {
        return { incident: inside, gap: 0 };
    }

    return nearestIncident(player.position);
}

// Fire.virtualWorld of a fire every world sees.
const EVERY_WORLD = 4294967295;

/**
 * Whether a fire burns where the player is: the same coordinates under the
 * map hold a building's rooms for every home and every call, each its own
 * world (a villa's hearth burns in its owner's world).
 */
export function sameWorld(fire: Fire, player: Player): boolean {
    // As unsigned: every world reads 4294967295, or -1 where it was assigned so.
    const world = fire.virtualWorld >>> 0;

    return world === EVERY_WORLD || world === player.virtualWorld >>> 0;
}

function nearestTo(player: Player, points: Point[]): Point | null {
    return (
        points.sort((a, b) => distance(a, player.position) - distance(b, player.position))[0] ??
        null
    );
}

// Inside a building: someone trapped in this room, else a fire in it, else
// the door towards a room still burning or with someone in it, else out.
function buildingTarget(incident: Incident, building: BuildingFire, player: Player): Point {
    const room = indoors.get(player.id)?.room ?? building.entry;
    const people = incident.civilians.filter(
        (civilian) => civilian.state === 'trapped' && civilian.npc,
    );
    const here = nearestTo(
        player,
        people
            .filter((civilian) => civilian.room === room)
            .flatMap((civilian) => (civilian.npc ? [civilian.npc.position] : [])),
    );

    if (here) {
        return here;
    }

    const flames = nearestTo(
        player,
        firesOf(incident)
            .filter((fire) => fire.burning && building.roomOf.get(fire.id) === room)
            .map((fire) => fire.position),
    );

    if (flames) {
        return flames;
    }

    const busy = (key: rooms.RoomKey): boolean =>
        people.some((civilian) => civilian.room === key) ||
        firesOf(incident).some((fire) => fire.burning && building.roomOf.get(fire.id) === key);
    const doors = rooms.ROOMS[room].doors;
    const door =
        doors.find((entry) => entry.to !== 'street' && busy(entry.to)) ??
        // Nothing left in here or beyond: back towards the street.
        doors.find((entry) => entry.to === 'street') ??
        doors[0];

    return door ? rooms.toWorld(room, door.at) : player.position;
}

/**
 * Where the compass points for this call: the fire, or the arsonist once it
 * is out, inside a burning bar the nearest one still waiting there, inside a
 * burning building the way through its rooms.
 */
export function targetOf(incident: Incident, player: Player): Point {
    const arsonist = incident.arsonist;

    if (arsonist?.state === 'fleeing' && arsonist.npc && !burning(incident)) {
        return arsonist.npc.position;
    }

    if (incident.building && burningInterior(player) === incident) {
        return buildingTarget(incident, incident.building, player);
    }

    if (incident.bar && burningInterior(player) === incident) {
        const inside = incident.civilians
            .filter((civilian) => civilian.state === 'trapped')
            .flatMap((civilian) => (civilian.npc ? [civilian.npc] : []))
            .sort(
                (a, b) =>
                    distance(a.position, player.position) - distance(b.position, player.position),
            )[0];

        return inside?.position ?? incident.bar.interior.inside.position;
    }

    return incident.position;
}
