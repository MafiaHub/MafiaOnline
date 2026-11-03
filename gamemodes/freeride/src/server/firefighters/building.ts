// A building on fire with people inside. One of the homes Free Ride sells
// (housing/catalogue.ts) catches fire: flames out of the ground floor beside
// its street door, and inside, in the furnished rooms every home of its kind
// shares (lib/house_rooms.ts), fires on the furniture and residents caught in
// the smoke. The crew goes in through the door (G), finds them room by room,
// leads them out through the exit arrow and puts the rooms out by hand with
// the extinguisher (hose.ts), which is refilled at a truck. Left alone the
// fire spreads to the next piece of furniture, and after a few minutes the
// smoke takes whoever is still lying inside.
//
// Each call has its own virtual world over the shared rooms, so the fires,
// the residents and the crew of one call never meet a home's owner or
// another call. Only vacant homes nobody is in burn.
import * as life from '../lib/life';
import * as rooms from '../lib/house_rooms';
import { here, title, type House } from '../housing/catalogue';
import { homeOf } from '../housing/homes';
import { occupants } from '../housing/occupancy';
import { CROUCHED, LYING } from '../lib/civilians';
import { npcsAvailable } from '../lib/npcs';
import { COLORS, arrow, feed, markers, text, toast, toastAll } from '../lib/notify';
import { Transfers } from '../lib/transfer';
import { MAIN_WORLD, distance, flatDistance, pick, playable, plain, type Point } from '../lib/util';
import { crews } from './duty';
import { presentIncidents } from './markers';
import {
    burning,
    incidents,
    indoors,
    newIncident,
    type BuildingFire,
    type FireSpot,
    type Incident,
} from './model';
import { INSIDE_DOOR_REACH, PLANS, inward } from './plans';
import { bringOut, carryThrough, followersOf, trap } from './rescue';

const FIRST_WORLD = 0x54000000;
const WORLDS = 0x10000;
// Where the call comes in from the crew, as the street fires (lib/callouts.ts).
const MIN_DISTANCE = 250;
const MAX_DISTANCE = 1100;
const DOOR_REACH = 2.0;
// The flames out of the ground floor stand this far along the facade from the door.
const FACADE_OFFSET = 3.2;
const SPREAD_FIRST_MS = 60_000;
const SPREAD_EVERY_MS = 70_000;
// Fires burning inside at once, at most (Fire.create allows 64 in all).
const MAX_INSIDE = 7;
const SMOKE_MS = 5 * 60_000;
const SMOKE_WARNING_MS = 60_000;
const MOVE_RETRIES = 5;
const RETRY_MS = 1000;

// What the neighbours told the dispatcher: the same call never reads the same twice in a row.
const STORIES: readonly string[] = [
    'A neighbour smelled smoke and heard coughing behind the door.',
    'A tenant fell asleep with a cigarette.',
    'A kerosene lamp tipped over, the landlord says.',
    'Someone is banging on a window from the inside.',
    'The wiring has been sparking for weeks, says the woman next door.',
    'A boy ran to the call box: his grandmother is still in there.',
];

const moves = new Transfers();
let lastStory = '';

function shuffled<T>(list: readonly T[]): T[] {
    const copy = [...list];

    for (let index = copy.length - 1; index > 0; index--) {
        const other = Math.floor(Math.random() * (index + 1));

        [copy[index], copy[other]] = [copy[other], copy[index]];
    }

    return copy;
}

function worldOf(incident: Incident): number {
    return FIRST_WORLD + (incident.id % WORLDS);
}

// A vacant home nobody is in, not already burning, the call's distance from the crew if one is.
function chooseHouse(origin: Point | null): House | null {
    const burningIds = new Set(
        [...incidents.values()].flatMap((incident) =>
            incident.building ? [incident.building.houseId] : [],
        ),
    );
    const vacant = here().filter(
        (house) => !homeOf(house) && !occupants(house).length && !burningIds.has(house.id),
    );
    const inRange = origin
        ? vacant.filter((house) => {
              const gap = flatDistance(house.door, origin);

              return gap >= MIN_DISTANCE && gap <= MAX_DISTANCE;
          })
        : vacant;

    return pick(inRange.length ? inRange : vacant) ?? null;
}

// Along the facade from the door: +1 to the right looking out of it.
function along(house: House, side: number, metres: number, out = 0): Point {
    const { heading } = house;

    return {
        x: house.door.x + Math.cos(heading) * side * metres + Math.sin(heading) * out,
        y: house.door.y,
        z: house.door.z - Math.sin(heading) * side * metres + Math.cos(heading) * out,
    };
}

function ignite(incident: Incident, building: BuildingFire, spot: FireSpot): Fire | null {
    const floor = rooms.ROOMS[spot.room].bounds.floor;
    const fire = Fire.create(rooms.toWorld(spot.room, { x: spot.x, y: floor + 0.05, z: spot.z }), {
        radius: spot.radius,
        height: spot.height,
        toughness: 1.2,
        growth: 0.01,
        damage: 10,
        virtualWorld: building.world,
    });

    if (fire) {
        incident.extra.push(fire);
        building.roomOf.set(fire.id, spot.room);
    }

    return fire;
}

export function startBuildingFire(origin: Point | null): Incident | null {
    if (!crews.size || !npcsAvailable() || !rooms.isBuilt()) {
        return null;
    }

    const house = chooseHouse(origin);

    if (!house) {
        return null;
    }

    const side = Math.random() < 0.5 ? 1 : -1;
    const facade = Fire.create(along(house, side, FACADE_OFFSET, 0.6), {
        radius: 1.8,
        height: 5,
        toughness: 3,
        growth: 0.012,
        damage: 18,
    });

    if (!facade) {
        return null;
    }

    const kind = rooms.KINDS[house.tier];
    const story = pick(STORIES.filter((entry) => entry !== lastStory)) ?? STORIES[0];

    lastStory = story;
    const incident = newIncident(
        'building',
        plain(house.door),
        'BUILDING FIRE · PEOPLE INSIDE',
        facade,
    );
    const now = Date.now();
    const building: BuildingFire = {
        houseId: house.id,
        name: title(house),
        tier: house.tier,
        world: worldOf(incident),
        rooms: kind.rooms,
        entry: kind.entry,
        door: plain(house.door),
        out: house.out,
        street: [-1.5, 0, 1.5].map((offset) => along(house, offset, 1, 3.5)),
        roomOf: new Map(),
        spare: [],
        nextSpreadAt: now + SPREAD_FIRST_MS,
        smokeAt: now + SMOKE_MS,
        smokeWarned: false,
        story,
    };

    incident.building = building;
    incidents.set(incident.id, incident);

    for (const room of kind.rooms) {
        const plan = PLANS[room];
        const spots = shuffled(plan.fires).map((spot) => ({ ...spot, room }));

        spots.slice(0, plan.lit).forEach((spot) => ignite(incident, building, spot));
        building.spare.push(...spots.slice(plan.lit));
    }

    // One resident in a tenement room, one or two in a flat, two somewhere in a villa.
    const count =
        house.tier === 'poor' ? 1 : house.tier === 'middle' ? 1 + Number(Math.random() < 0.5) : 2;
    const places = shuffled(
        kind.rooms.flatMap((room) => PLANS[room].people.map((spot) => ({ room, ...spot }))),
    ).slice(0, count);

    places.forEach((spot, index) => {
        const floor = rooms.ROOMS[spot.room].bounds.floor;

        trap(
            incident,
            rooms.toWorld(spot.room, { x: spot.x, y: floor + 0.2, z: spot.z }),
            Math.random() < 0.5 ? LYING : CROUCHED,
            index,
            { world: building.world, room: spot.room },
        );
    });

    toastAll(
        `${building.name} is burning ${incident.where}, people still inside. ${story} ` +
            'Firefighters: G at the door to go in, the extinguisher for the rooms.',
        { title: 'Fire', tone: 'bad', seconds: 10 },
    );

    feed('fire', [
        text('Fire', COLORS.red),
        text(` · ${building.name} is burning with people inside`),
    ]);

    presentIncidents();

    return incident;
}

/** The burning building whose street door the crew member stands at, on foot. */
export function buildingAtDoor(player: Player): Incident | null {
    if (player.getVehicle() || player.virtualWorld !== MAIN_WORLD || moves.isMoving(player)) {
        return null;
    }

    for (const incident of incidents.values()) {
        const building = incident.building;

        if (
            building &&
            flatDistance(building.door, player.position) <= DOOR_REACH &&
            Math.abs(building.door.y - player.position.y) < 3
        ) {
            return incident;
        }
    }

    return null;
}

/** The door of the burning building's room the player stands at. */
export function doorInside(
    player: Player,
): { incident: Incident; building: BuildingFire; door: rooms.RoomDoor } | null {
    const spot = indoors.get(player.id);
    const incident = spot ? incidents.get(spot.incidentId) : undefined;
    const building = incident?.building;

    if (!spot || !incident || !building || moves.isMoving(player)) {
        return null;
    }

    const door = rooms.ROOMS[spot.room].doors.find(
        (entry) =>
            distance(player.position, rooms.toWorld(spot.room, entry.at)) <= INSIDE_DOOR_REACH,
    );

    return door ? { incident, building, door } : null;
}

/** The prompt line at a door: in, out, or through to the next room. */
export function doorHint(player: Player): string | null {
    if (buildingAtDoor(player)) {
        return '[G] GO IN';
    }

    const inside = doorInside(player);

    if (!inside) {
        return null;
    }

    return inside.door.to === 'street' ? '[G] GO OUT' : `[G] ${inside.door.label}`;
}

function doorMarkers(player: Player, room: rooms.RoomKey): void {
    markers(
        player,
        'fire-doors',
        rooms.ROOMS[room].doors.map((door, index) =>
            arrow(
                `d${index}`,
                rooms.toWorld(room, door.at),
                door.to === 'street' ? 'EXIT' : door.label,
                door.to === 'street' ? COLORS.green : COLORS.gold,
                14,
                { near: 10, map: false },
            ),
        ),
    );
}

export function enter(player: Player, incident: Incident): void {
    const building = incident.building;

    if (!building || !playable(player)) {
        return;
    }

    const entry = rooms.ROOMS[building.entry];

    moves.move(player, inward(rooms.pose(building.entry, entry.arrive)), building.world, {
        then: (moved) => {
            if (!moved) {
                return;
            }

            // The call ended during the fade: nothing left in there.
            if (!incidents.has(incident.id)) {
                // Kept until out, so a refused move is tried again.
                indoors.set(player.id, { incidentId: incident.id, room: building.entry });
                moveOut(player, building);

                return;
            }

            indoors.set(player.id, { incidentId: incident.id, room: building.entry });
            doorMarkers(player, building.entry);
            toast(
                player,
                'Thick smoke. Find the residents, lead them to the exit arrow, and put the rooms out: hold G for the extinguisher.',
                { title: building.name, tone: 'bad', seconds: 7 },
            );
        },
    });
}

/** Through a door: out to the street, or into the next room with those close behind. */
export function useDoor(
    player: Player,
    incident: Incident,
    building: BuildingFire,
    door: rooms.RoomDoor,
): void {
    const spot = indoors.get(player.id);

    if (!spot) {
        return;
    }

    const followers = followersOf(player, incident);

    if (door.to === 'street') {
        for (const civilian of followers) {
            bringOut(incident, building.street, civilian, building.out.heading);
        }

        moveOut(player, building);

        return;
    }

    const to = door.to;
    const target = rooms.ROOMS[to];
    const arrival = inward(rooms.pose(to, target.returns?.[spot.room] ?? target.arrive));

    moves.move(player, arrival, building.world, {
        then: (moved) => {
            // The call ended during the fade: its people are gone and its
            // evacuation is already taking the player out.
            if (
                !moved ||
                !incidents.has(incident.id) ||
                indoors.get(player.id)?.incidentId !== incident.id
            ) {
                return;
            }

            indoors.set(player.id, { incidentId: incident.id, room: to });
            doorMarkers(player, to);

            for (const civilian of followers) {
                if (civilian.state === 'following') {
                    carryThrough(civilian, to, arrival, building.world);
                }
            }
        },
    });
}

// Out to the street in front of the door; a move a loading client refused is tried again.
function moveOut(player: Player, building: BuildingFire, attempt = 0): void {
    moves.move(player, building.out, MAIN_WORLD, {
        then: (moved) => {
            if (moved) {
                indoors.delete(player.id);
                markers(player, 'fire-doors', []);
            } else if (attempt < MOVE_RETRIES) {
                setTimeout(() => {
                    if (indoors.has(player.id) && player.alive) {
                        moveOut(player, building, attempt + 1);
                    }
                }, RETRY_MS);
            } else {
                drop(player, building);
            }
        },
    });
}

// Straight out without the fade: the resource stops, or the moves kept failing.
function drop(player: Player, building: BuildingFire): void {
    indoors.delete(player.id);
    moves.forget(player);
    markers(player, 'fire-doors', []);

    if (player.virtualWorld === building.world) {
        player.setVirtualWorld(MAIN_WORLD);

        if (player.alive) {
            life.teleport(player, building.out.position, building.out.heading);
        }
    }
}

function inside(incident: Incident): Player[] {
    return World.getPlayers().filter(
        (player) => indoors.get(player.id)?.incidentId === incident.id,
    );
}

/** The call is over: everyone still inside comes out, at once when `now`. */
export function evacuate(incident: Incident, now = false): void {
    const building = incident.building;

    if (!building) {
        return;
    }

    for (const player of inside(incident)) {
        if (now) {
            drop(player, building);
        } else {
            toast(player, 'The building is clear. You step back out.', {
                title: building.name,
                seconds: 4,
            });

            moveOut(player, building);
        }
    }
}

/** Off duty or dead inside: out of the call's world without a walk. */
export function release(player: Player): void {
    const spot = indoors.get(player.id);
    const building = spot ? incidents.get(spot.incidentId)?.building : null;

    if (!spot) {
        return;
    }

    if (building && player.alive) {
        moveOut(player, building);

        return;
    }

    indoors.delete(player.id);
    moves.forget(player);
    markers(player, 'fire-doors', []);

    if (player.virtualWorld >= FIRST_WORLD && player.virtualWorld < FIRST_WORLD + WORLDS) {
        player.setVirtualWorld(MAIN_WORLD);
    }
}

/** The mission change took every fire, person and world with it. */
export function forgetAll(): void {
    for (const player of World.getPlayers()) {
        if (indoors.has(player.id) && player.virtualWorld >= FIRST_WORLD) {
            player.setVirtualWorld(MAIN_WORLD);
        }
    }

    indoors.clear();
    moves.clear();
}

function tellCrew(incident: Incident, message: string, tone: 'bad' | 'good' = 'bad'): void {
    const building = incident.building;

    for (const [player] of crews.crew()) {
        const gap = flatDistance(player.position, incident.position);

        if (indoors.get(player.id)?.incidentId === incident.id || gap < 300) {
            toast(player, message, { title: building?.name ?? 'Fire', tone, seconds: 6 });
        }
    }
}

// The fire takes the next piece of furniture, in a room already burning when it can.
function spread(incident: Incident, building: BuildingFire, now: number): void {
    building.nextSpreadAt = now + SPREAD_EVERY_MS;
    const alight = incident.extra.filter((fire) => fire.burning);

    if (!building.spare.length || alight.length >= MAX_INSIDE) {
        return;
    }

    const burningRooms = new Set(alight.map((fire) => building.roomOf.get(fire.id)));
    const near = building.spare.filter((spot) => burningRooms.has(spot.room));
    const spot = pick(near.length ? near : building.spare);

    if (!spot) {
        return;
    }

    building.spare = building.spare.filter((entry) => entry !== spot);

    if (ignite(incident, building, spot)) {
        tellCrew(incident, `The fire has spread to ${spot.what} in ${PLANS[spot.room].label}.`);
        presentIncidents();
    }
}

// The smoke: a warning a minute before, then whoever is still lying inside is lost.
function smoke(incident: Incident, building: BuildingFire, now: number): void {
    const lying = incident.civilians.filter(
        (civilian) => civilian.state === 'trapped' && civilian.npc,
    );

    if (!lying.length) {
        return;
    }

    if (!building.smokeWarned && now >= building.smokeAt - SMOKE_WARNING_MS) {
        building.smokeWarned = true;
        tellCrew(incident, 'The smoke is getting thick: a minute left for those still inside.');
    }

    if (now < building.smokeAt) {
        return;
    }

    for (const civilian of lying) {
        civilian.state = 'dead';
        civilian.npc?.setHealth(0);
    }

    tellCrew(
        incident,
        lying.length > 1
            ? 'The smoke got the ones still lying inside.'
            : 'The smoke got the one still lying inside.',
    );

    feed('fire', [
        text('Fire', COLORS.muted),
        text(` · the smoke took someone in ${building.name}`),
    ]);

    presentIncidents();
}

/** Each tick for a burning building: the spread and the smoke. */
export function updateBuilding(incident: Incident, now: number): void {
    const building = incident.building;

    if (!building) {
        return;
    }

    if (burning(incident) && now >= building.nextSpreadAt) {
        spread(incident, building, now);
    }

    smoke(incident, building, now);
}

/** What the card says of a burning building for this firefighter. */
export function buildingRows(
    player: Player,
    incident: Incident,
    now: number,
): { subtitle: string; rows: [string, string][] } | null {
    const building = incident.building;

    if (!building) {
        return null;
    }

    const rows: [string, string][] = [];
    const lying = incident.civilians.filter((civilian) => civilian.state === 'trapped').length;
    const spot = indoors.get(player.id);

    if (lying && now < building.smokeAt) {
        const seconds = Math.ceil((building.smokeAt - now) / 1000);

        rows.push([
            'Smoke',
            `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`,
        ]);
    }

    let subtitle: string;

    if (spot?.incidentId === incident.id) {
        const room = PLANS[spot.room].label;

        subtitle = lying
            ? `Someone is still inside: search the rooms`
            : burning(incident)
              ? `Put out the fires in ${room}`
              : 'All out. Back to the street';
    } else {
        subtitle = lying
            ? `People inside ${building.name}: G at the door`
            : `${building.name} is burning`;
    }

    return { subtitle, rows };
}
