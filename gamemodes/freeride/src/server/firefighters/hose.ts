// The water. Firefighters put fires out with the hose (hold G within reach
// of any fire truck, or from a truck's roof cannon while at its wheel): the
// native water jet arcs onto the fire and every hose on it takes the flames
// down, so a crew is faster than one.
import type { JobKeyHandler } from '../lib/jobkey';
import { toast } from '../lib/notify';
import { flatDistance, headingOf, playable, type Point } from '../lib/util';
import { crews, station, type Crew } from './duty';
import { burning, burningInterior, nearestIncident, type Incident } from './model';
import { leadOut, rescueNear } from './rescue';

export const HOSE_TO_TRUCK = 35;
const HOSE_REACH = 24;
const CANNON_REACH = 32;

interface Hose {
    jet: WaterJet;
    reach: number;
    from: Point;
    onFoot: boolean;
}

/**
 * The fire truck nearest the player, for the hose and the card. Inside a
 * burning bar the hose runs in through its door from a truck outside.
 */
export function nearestTruck(player: Player): { vehicle: Vehicle; gap: number } | null {
    const indoors = burningInterior(player)?.bar;

    return station.nearestVehicle(indoors ? indoors.interior.outside.position : player.position);
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
 * truck's driver seat, the hose on foot within reach of any fire truck.
 */
export function hoseFor(player: Player, crew: Crew): Hose | null {
    const vehicle = player.getVehicle();

    if (vehicle) {
        return crew.cannon && crew.cannonTruckId === vehicle.id && player.getSeat() === 0
            ? { jet: crew.cannon, reach: CANNON_REACH, from: vehicle.position, onFoot: false }
            : null;
    }

    const truck = nearestTruck(player);

    if (!truck || truck.gap > HOSE_TO_TRUCK) {
        return null;
    }

    return { jet: crew.hose, reach: HOSE_REACH, from: player.position, onFoot: true };
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

    const targets = Fire.getInRange(hose.from, hose.reach + 15)
        .filter((fire) => fire.burning)
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

        return;
    }

    // Nothing in reach: the water goes where the nozzle points.
    const heading = headingOf((player.getVehicle() ?? player).rotation);

    hose.jet.aimAt({
        x: hose.from.x + Math.sin(heading) * 12,
        y: hose.from.y,
        z: hose.from.z + Math.cos(heading) * 12,
    });
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

/** G for a firefighter: lead a trapped person out, or spray. */
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

        // Nothing of the brigade's here: the key goes to whatever else is
        // (the rum still, a side job). The roof cannon at a truck's wheel
        // always answers it.
        const hose = hoseFor(player, crew);
        const near = fireNear(player, nearestIncident(player.position), hose);

        if (!hose) {
            if (!near) {
                return false;
            }

            toast(player, 'Too far from a fire truck: the hose reaches 35 m from one.', {
                tone: 'bad',
                seconds: 3,
            });

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
