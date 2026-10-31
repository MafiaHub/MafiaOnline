// People caught in a fire: a crash's passengers on the road, a resident at
// the house door, the barman and drinkers of a burning bar, the residents
// somewhere in the rooms of a burning building. G next to one leads them
// out; well clear of the fire (or, out of a bar or a building, through its
// exit arrow) they are safe, and a medic on duty takes them from there.
import * as interiors from '../interiors';
import * as profiles from '../lib/profiles';
import * as rooms from '../lib/house_rooms';
import { COLORS, feed, name, text, toast } from '../lib/notify';
import { events } from '../lib/lifecycle';
import { CIVILIANS, CROUCHED, PANIC_LINES, say, spawnCivilian, voiceFor } from '../lib/civilians';
import { walkOff } from '../lib/npcs';
import {
    distance,
    flatDistance,
    formatMoney,
    pick,
    playerById,
    type Placement,
    type Point,
} from '../lib/util';
import { earn } from './duty';
import { presentIncidents } from './markers';
import { incidents, indoors, type Civilian, type Incident } from './model';

const RESCUE_REACH = 3;
const SAFE_DISTANCE = 12;
// A civilian led this close to the exit arrow inside is out.
const EXIT_REACH = 3.5;
// A building's rooms are small: out only once at its door.
const BUILDING_EXIT_REACH = 1.8;
const RESCUE_PAY = 120;
// Each one led out of a burning bar: three of them, and the bonus besides.
const BAR_RESCUE_PAY = 50;
// A smoke victim out of a bar or a building carries this much more on the medic's fare.
const INDOOR_MEDIC_BONUS = 40;
const RESCUE_XP = 40;
const PANIC_EVERY_MS = 9000;

// How close behind their leader someone being led goes through a door with them.
export const CARRY_REACH = 5;

/** The ambulance's side of a hand-over (ambulance/scenes.ts). */
export interface AmbulanceDesk {
    /** A medic on duty takes the person; false when nobody can. `bonus` rides on the medic's fare. */
    takePatient(npc: Npc, position: Point, label: string, options?: { bonus?: number }): boolean;
}

let ambulance: AmbulanceDesk | null = null;

export function setAmbulance(desk: AmbulanceDesk): void {
    ambulance = desk;
}

export interface TrapOptions {
    model?: string;
    heading?: number;
    /** The world of a bar or a building built apart from the street. */
    world?: number;
    /** The room of a burning building they are in. */
    room?: rooms.RoomKey;
}

/** Someone caught in the fire, at `position`, in the clip they wait in. */
export function trap(
    incident: Incident,
    position: Point,
    clip: string,
    index: number,
    {
        model = pick(CIVILIANS),
        heading = Math.random() * Math.PI * 2,
        world,
        room,
    }: TrapOptions = {},
): void {
    const voice = voiceFor(model);
    const npc = spawnCivilian(model, position, heading, { voice, world });

    if (!npc) {
        return;
    }

    incident.civilians.push({
        index,
        npc,
        ready: false,
        clip,
        model,
        voice,
        state: 'spawning',
        leaderId: null,
        nextLineAt: Date.now() + 3000 + Math.random() * PANIC_EVERY_MS,
        outside: null,
        outsideReady: false,
        room: room ?? null,
        relead: false,
    });
}

export function rescueNear(player: Player): { incident: Incident; civilian: Civilian } | null {
    for (const incident of incidents.values()) {
        for (const civilian of incident.civilians) {
            if (
                civilian.state === 'trapped' &&
                civilian.npc &&
                civilian.npc.virtualWorld === player.virtualWorld &&
                distance(civilian.npc.position, player.position) <= RESCUE_REACH
            ) {
                return { incident, civilian };
            }
        }
    }

    return null;
}

export function leadOut(player: Player, incident: Incident, civilian: Civilian): void {
    civilian.state = 'following';
    civilian.leaderId = player.id;
    civilian.npc?.stopAnimation();
    civilian.npc?.follow(player, 1.6);
    toast(
        player,
        incident.bar || incident.building
            ? 'They are on their feet. Lead them to the exit arrow; they come out with you.'
            : 'They are on their feet. Lead them well clear of the fire.',
        { title: 'Rescue', tone: 'gold' },
    );

    presentIncidents();
}

/** Those following this player closely enough to go through a door with them. */
export function followersOf(player: Player, incident: Incident): Civilian[] {
    return incident.civilians.filter(
        (civilian) =>
            civilian.state === 'following' &&
            civilian.leaderId === player.id &&
            civilian.npc !== null &&
            distance(civilian.npc.position, player.position) <= CARRY_REACH,
    );
}

/**
 * Through a door between two rooms with their leader: the person is made
 * again just behind where the leader arrives, and follows once there.
 */
export function carryThrough(
    civilian: Civilian,
    room: rooms.RoomKey,
    arrival: Placement,
    world: number,
): void {
    const behind = {
        x: arrival.position.x - Math.sin(arrival.heading) * 1.2,
        y: arrival.position.y,
        z: arrival.position.z - Math.cos(arrival.heading) * 1.2,
    };

    civilian.npc?.remove();
    civilian.npc = spawnCivilian(civilian.model, behind, arrival.heading, {
        voice: civilian.voice,
        world,
    });

    civilian.ready = false;
    civilian.room = room;

    if (civilian.npc) {
        civilian.state = 'spawning';
        civilian.relead = true;
    } else {
        civilian.state = 'gone';
    }
}

/**
 * Pays the one who led them out; `handOff` passes them on to a medic or
 * sends them home now (a bar's civilians first come out onto the pavement).
 */
function rescued(incident: Incident, civilian: Civilian, handOff = true): void {
    const player = playerById(civilian.leaderId);

    civilian.state = 'safe';

    if (player) {
        incident.rescuers.add(player.id);
        const pay = incident.kind === 'bar' ? BAR_RESCUE_PAY : RESCUE_PAY;

        player.giveMoney(pay);
        earn(player, pay);
        profiles.bump(player, 'rescues');
        profiles.addXp(player, RESCUE_XP, 'Led someone out of a fire');
        toast(player, `Safe. ${formatMoney(pay)} from the city.`, {
            title: 'Rescue',
            tone: 'good',
        });

        feed('fire', [name(player, COLORS.red), text(' pulled someone out of a fire')]);
    }

    if (!handOff) {
        return;
    }

    const npc = civilian.npc;

    civilian.npc = null;
    passOn(incident, npc);
}

/**
 * A medic on duty takes them to hospital; otherwise they go home. Out of a
 * bar or a building, the medic's call carries a share of the big call's bonus.
 */
function passOn(incident: Incident, npc: Npc | null): void {
    if (!npc) {
        return;
    }

    const place = incident.bar?.name ?? incident.building?.name ?? null;
    const label = place ? `Smoke victim from ${place}` : `Smoke victim ${incident.where}`;

    if (
        !ambulance?.takePatient(npc, incident.position, label, {
            bonus: place ? INDOOR_MEDIC_BONUS : 0,
        })
    ) {
        // Out of a bar, along the pavement rather than back into the building.
        const { x, y, z } = npc.position;

        walkOff(npc, 20_000, { to: { x: x + 20, y, z: z + (place ? 0 : 20) } });
    }
}

/**
 * Led to the exit arrow, a civilian comes out onto the pavement: the one
 * inside goes and the same person stands outside, coughing.
 */
export function bringOut(
    incident: Incident,
    street: readonly Point[],
    civilian: Civilian,
    heading = Math.PI,
): void {
    civilian.npc?.remove();
    civilian.npc = null;
    civilian.room = null;
    rescued(incident, civilian, false);
    const spot = street[civilian.index % street.length];
    const npc = spot
        ? spawnCivilian(civilian.model, spot, heading, { voice: civilian.voice })
        : null;

    if (!npc) {
        return;
    }

    civilian.outside = npc;
    civilian.state = 'outside';
}

/** The people of one call, each tick: lying down, following, crying for help. */
export function updateCivilians(incident: Incident, now: number): void {
    for (const civilian of incident.civilians) {
        const npc = civilian.npc;

        // Trapped civilians lie down once the host made them; one made again
        // in the next room follows on.
        if (civilian.state === 'spawning' && npc && civilian.ready) {
            const relead = civilian.relead;
            const leader = relead ? playerById(civilian.leaderId) : null;

            civilian.relead = false;

            if (leader?.alive) {
                civilian.state = 'following';
                npc.follow(leader, 1.6);
            } else {
                civilian.state = 'trapped';
                npc.playAnimation(relead ? CROUCHED : civilian.clip, { loop: true });
            }
        } else if (civilian.state === 'following' && npc) {
            follow(incident, civilian, npc);
        } else if (civilian.state === 'outside' && civilian.outside && civilian.outsideReady) {
            const outside = civilian.outside;

            civilian.outside = null;
            civilian.state = 'safe';
            passOn(incident, outside);
        }

        // Cries for help while they wait.
        if (civilian.state === 'trapped' && civilian.npc && now >= civilian.nextLineAt) {
            civilian.nextLineAt = now + PANIC_EVERY_MS * (0.7 + Math.random() * 0.6);
            say(civilian.npc, PANIC_LINES);
        }
    }
}

// Back down where they are: their leader is gone, dead, or went on without them.
function leftBehind(civilian: Civilian, npc: Npc, leader: Player | null, why: string): void {
    civilian.state = 'trapped';
    npc.stop();
    npc.playAnimation(CROUCHED, { loop: true });

    if (leader) {
        toast(leader, why, { title: 'Rescue', tone: 'bad' });
    }
}

function follow(incident: Incident, civilian: Civilian, npc: Npc): void {
    if (incident.building) {
        followInBuilding(incident, incident.building, civilian, npc);

        return;
    }

    const leader = playerById(civilian.leaderId);
    const bar = incident.bar;
    // Out of a bar through its exit arrow; a leader who went out without
    // them leaves them behind.
    const behind = bar && leader && interiors.insideOf(leader)?.id !== bar.interior.id;
    const exit = bar?.interior.inside.position;

    if (
        bar &&
        exit &&
        flatDistance(npc.position, exit) <= EXIT_REACH &&
        Math.abs(npc.position.y - exit.y) < 3
    ) {
        bringOut(incident, bar.street, civilian);
    } else if (!leader || !leader.alive || behind) {
        leftBehind(
            civilian,
            npc,
            behind ? leader : null,
            'You left them behind inside. Go back in and bring them to the exit arrow.',
        );
    } else if (!bar && flatDistance(npc.position, incident.position) >= SAFE_DISTANCE) {
        rescued(incident, civilian);
    }
}

// Inside a building: out through the street door of the room people come in
// by; a leader who left the room without them leaves them behind.
function followInBuilding(
    incident: Incident,
    building: NonNullable<Incident['building']>,
    civilian: Civilian,
    npc: Npc,
): void {
    const leader = playerById(civilian.leaderId);
    const spot = leader ? indoors.get(leader.id) : undefined;
    const exitDoor = rooms.ROOMS[building.entry].doors.find((door) => door.to === 'street');
    const exit = exitDoor ? rooms.toWorld(building.entry, exitDoor.at) : null;

    if (
        civilian.room === building.entry &&
        exit &&
        flatDistance(npc.position, exit) <= BUILDING_EXIT_REACH &&
        Math.abs(npc.position.y - exit.y) < 3
    ) {
        bringOut(incident, building.street, civilian, building.out.heading);
    } else if (!leader || !leader.alive) {
        leftBehind(civilian, npc, null, '');
    } else if (spot?.incidentId !== incident.id || spot.room !== civilian.room) {
        leftBehind(
            civilian,
            npc,
            leader,
            'You left them behind. Go back for them and keep them close through the doors.',
        );
    }
}

events.on('npcSpawn', (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc?.id === npc.id);

        if (civilian) {
            civilian.ready = true;
        }

        const out = incident.civilians.find((entry) => entry.outside?.id === npc.id);

        if (out) {
            out.outsideReady = true;
        }
    }
});

events.on('npcDeath', (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc?.id === npc.id);

        if (civilian) {
            civilian.state = 'dead';
        }
    }
});

// A civilian the host could not make, or lost: nobody is left to lead out.
events.on('npcRetire', (npc) => {
    for (const incident of incidents.values()) {
        const civilian = incident.civilians.find((entry) => entry.npc?.id === npc.id);

        if (civilian) {
            civilian.npc = null;

            if (civilian.state !== 'dead') {
                civilian.state = 'gone';
            }
        }

        // Out on the pavement already: they went on their own.
        const out = incident.civilians.find((entry) => entry.outside?.id === npc.id);

        if (out) {
            out.outside = null;
            out.state = 'safe';
        }
    }
});
