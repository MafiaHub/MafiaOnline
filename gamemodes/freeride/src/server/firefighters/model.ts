// What the fire brigade answers. A call (an incident) is one or more fires,
// the people caught in them and, now and then, the arsonist who set it.
import * as interiors from '../interiors';
import type { Bar } from '../bars';
import { where } from '../lib/callouts';
import type { MapInterior } from '../lib/maps';
import { distance, flatDistance, type Point } from '../lib/util';

export type IncidentKind = 'house' | 'car' | 'crash' | 'pump' | 'bar' | 'pileup';

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
    fields: Partial<Pick<Incident, 'vehicleId' | 'extra' | 'bar' | 'pumps' | 'keepVehicle'>> = {},
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

/** The bar on fire whose room the player is in. */
export function burningInterior(player: Player): Incident | null {
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
 * Where the compass points for this call: the fire, or the arsonist once it
 * is out, or inside a burning bar the nearest one still waiting there.
 */
export function targetOf(incident: Incident, player: Player): Point {
    const arsonist = incident.arsonist;

    if (arsonist?.state === 'fleeing' && arsonist.npc && !burning(incident)) {
        return arsonist.npc.position;
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
