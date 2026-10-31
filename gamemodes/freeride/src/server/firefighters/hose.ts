// The water. Firefighters put fires out with the hose (hold G within reach
// of any fire truck, or from a truck's roof cannon while at its wheel): the
// native water jet arcs onto the fire and every hose on it takes the flames
// down, so a crew is faster than one. Indoors, in a burning bar or building,
// no hose reaches: they work by hand with the extinguisher, a short jet of
// limited charge that is refilled at any fire truck.
import type { JobKeyHandler } from '../lib/jobkey';
import { toast } from '../lib/notify';
import { MAIN_WORLD, flatDistance, headingOf, playable, type Point } from '../lib/util';
import { buildingAtDoor, doorInside, enter, useDoor } from './building';
import { crews, station, type Crew } from './duty';
import { burning, burningInterior, incidentNear, sameWorld, type Incident } from './model';
import { leadOut, rescueNear } from './rescue';

export const HOSE_TO_TRUCK = 35;
const HOSE_REACH = 24;
const CANNON_REACH = 32;
const HOSE_PRESSURE = 18;

/** Seconds of spray in a full extinguisher. */
export const EXTINGUISHER_SECONDS = 40;
// A low jet: it reaches about pressure squared over 9.8, some 8 m.
const EXTINGUISHER_PRESSURE = 9;
const EXTINGUISHER_REACH = 6;
// What the extinguisher takes off the fire it is on each spray tick (250 ms)
// on top of the jet's own water: a room fire is out in some seven seconds.
const EXTINGUISHER_DOUSE = 0.035;

// On foot this close to a fire truck refills it.
export const REFILL_REACH = 8;

interface Hose {
    jet: WaterJet;
    reach: number;
    from: Point;
    onFoot: boolean;
    extinguisher: boolean;
}

/**
 * The fire truck nearest the player, for the hose and the card. Inside a
 * burning bar or building, the one nearest its door.
 */
export function nearestTruck(player: Player): { vehicle: Vehicle; gap: number } | null {
    const indoors = burningInterior(player);
    const door = indoors?.bar?.interior.outside.position ?? indoors?.building?.door;

    return station.nearestVehicle(door ?? player.position);
}

/** Whether the player works indoors, where only the extinguisher reaches. */
export function indoorsNow(player: Player): boolean {
    return !player.getVehicle() && burningInterior(player) !== null;
}

// The point a hose aims at: the fire's edge nearest the nozzle, inside its
// area, so the water lands in the flames.
function aimPoint(fire: Fire, nozzle: Point): Point {
    const dx = nozzle.x - fire.position.x;
    const dz = nozzle.z - fire.position.z;
    const gap = Math.hypot(dx, dz) || 1;
    const inset = Math.min(gap, fire.radius * 0.5);

    return {
        x: fire.position.x + (dx / gap) * inset,
        y: fire.position.y + 0.6 + fire.height * 0.3,
        z: fire.position.z + (dz / gap) * inset,
    };
}

function facingTo(player: Player, point: Point): number {
    const heading = headingOf(player.rotation);
    const dx = point.x - player.position.x;
    const dz = point.z - player.position.z;
    const gap = Math.hypot(dx, dz) || 1;

    return (Math.sin(heading) * dx + Math.cos(heading) * dz) / gap;
}

/**
 * Where water from this firefighter can go now: the cannon from a fire
 * truck's driver seat, the hose on foot within reach of any fire truck, the
 * extinguisher indoors while it has charge.
 */
export function hoseFor(player: Player, crew: Crew): Hose | null {
    const vehicle = player.getVehicle();

    if (vehicle) {
        return crew.cannon && crew.cannonTruckId === vehicle.id && player.getSeat() === 0
            ? {
                  jet: crew.cannon,
                  reach: CANNON_REACH,
                  from: vehicle.position,
                  onFoot: false,
                  extinguisher: false,
              }
            : null;
    }

    if (indoorsNow(player)) {
        return crew.charge > 0
            ? {
                  jet: crew.hose,
                  reach: EXTINGUISHER_REACH,
                  from: player.position,
                  onFoot: true,
                  extinguisher: true,
              }
            : null;
    }

    const truck = nearestTruck(player);

    if (!truck || truck.gap > HOSE_TO_TRUCK) {
        return null;
    }

    return {
        jet: crew.hose,
        reach: HOSE_REACH,
        from: player.position,
        onFoot: true,
        extinguisher: false,
    };
}

export function spray(player: Player, crew: Crew): void {
    const hose = hoseFor(player, crew);

    if (!hose) {
        crew.hose.stop();
        crew.cannon?.stop();

        return;
    }

    if (hose.onFoot) {
        crew.cannon?.stop();
    } else {
        crew.hose.stop();
    }

    const pressure = hose.extinguisher ? EXTINGUISHER_PRESSURE : HOSE_PRESSURE;

    if (hose.onFoot && hose.jet.pressure !== pressure) {
        hose.jet.pressure = pressure;
    }

    const targets = Fire.getInRange(hose.from, hose.reach + 15)
        .filter((fire) => fire.burning && sameWorld(fire, player))
        .map((fire) => ({ fire, point: aimPoint(fire, hose.from) }))
        .filter((entry) => flatDistance(entry.point, hose.from) <= hose.reach)
        .map((entry) => ({
            ...entry,
            score:
                flatDistance(entry.point, hose.from) -
                (hose.onFoot ? facingTo(player, entry.point) * 10 : 0),
        }))
        .sort((a, b) => a.score - b.score);

    if (targets.length) {
        hose.jet.aimAt(targets[0].point);

        if (hose.extinguisher) {
            targets[0].fire.douse(EXTINGUISHER_DOUSE, player);
        }

        return;
    }

    // Nothing in reach: the water goes where the nozzle points.
    const heading = headingOf((player.getVehicle() ?? player).rotation);
    const throwTo = hose.extinguisher ? 4 : 12;

    hose.jet.aimAt({
        x: hose.from.x + Math.sin(heading) * throwTo,
        y: hose.from.y,
        z: hose.from.z + Math.cos(heading) * throwTo,
    });
}

/**
 * The extinguisher's charge, each tick: it runs down while it sprays and
 * fills up again next to a fire truck. `tickMs` is the shift's tick.
 */
export function tendExtinguisher(player: Player, crew: Crew, tickMs: number): void {
    if (crew.spraying && indoorsNow(player)) {
        crew.charge = Math.max(0, crew.charge - tickMs / 1000);

        if (crew.charge === 0) {
            crew.spraying = false;
            crew.hose.stop();
            toast(player, 'The extinguisher is empty. Refill it at a fire truck.', {
                tone: 'bad',
                seconds: 4,
            });
        }

        return;
    }

    if (
        crew.charge >= EXTINGUISHER_SECONDS ||
        player.getVehicle() ||
        player.virtualWorld !== MAIN_WORLD
    ) {
        return;
    }

    const truck = station.nearestVehicle(player.position);

    if (truck && truck.gap <= REFILL_REACH) {
        if (crew.charge < EXTINGUISHER_SECONDS * 0.9) {
            toast(player, 'Extinguisher refilled from the truck.', { tone: 'good', seconds: 3 });
        }

        crew.charge = EXTINGUISHER_SECONDS;
    }
}

// Whether a fire of this call is close enough to the player to hose.
function fireNear(
    player: Player,
    nearest: { incident: Incident; gap: number } | null,
    hose: Hose | null,
): boolean {
    return (
        nearest !== null &&
        burning(nearest.incident) &&
        (nearest.gap < (hose?.reach ?? HOSE_REACH) + 10 ||
            burningInterior(player) === nearest.incident)
    );
}

/** G for a firefighter: lead a trapped person out, go through a door, or spray. */
export const crewKey: JobKeyHandler = {
    press(player) {
        const crew = crews.get(player);

        if (!crew || !playable(player)) {
            return false;
        }

        const rescue = rescueNear(player);

        if (rescue && !player.getVehicle()) {
            leadOut(player, rescue.incident, rescue.civilian);

            return true;
        }

        const building = buildingAtDoor(player);

        if (building) {
            enter(player, building);

            return true;
        }

        const door = doorInside(player);

        if (door) {
            useDoor(player, door.incident, door.building, door.door);

            return true;
        }

        // Nothing of the brigade's here: the key goes to whatever else is
        // (the rum still, a side job). The roof cannon at a truck's wheel
        // always answers it.
        const hose = hoseFor(player, crew);
        const near = fireNear(player, incidentNear(player), hose);

        if (!hose) {
            if (!near) {
                return false;
            }

            toast(
                player,
                indoorsNow(player)
                    ? 'The extinguisher is empty. Refill it at a fire truck.'
                    : 'Too far from a fire truck: the hose reaches 35 m from one.',
                { tone: 'bad', seconds: 3 },
            );

            return true;
        }

        if (hose.onFoot && !near) {
            return false;
        }

        crew.spraying = true;
        spray(player, crew);

        return true;
    },

    release(player) {
        const crew = crews.get(player);

        if (!crew) {
            return;
        }

        crew.spraying = false;
        crew.hose.stop();
        crew.cannon?.stop();
    },
};
