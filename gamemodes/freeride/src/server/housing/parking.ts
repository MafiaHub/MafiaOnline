// Cars at the kerb: at a flat or a house the owner parks a car from their
// garage on the home's spot with G; it goes into the garage
// (server/dealership.ts) as it is, condition and all, and G on the spot
// brings it out there again for nothing, even after a restart.
import * as dealership from '../dealership';
import { toast } from '../lib/notify';
import { MAIN_WORLD, flatDistance, playable } from '../lib/util';
import { title, type House } from './catalogue';
import { ownedBy } from './homes';
import { blocked } from './occupancy';

/** How far from the spot a car is parked, and a player takes one out. */
export const PARK_REACH = 6;
export const TAKE_OUT_REACH = 4;

/** The player's home whose parking spot is within `reach`. */
export function parkingNear(player: Player, reach: number): House | null {
    return (
        ownedBy(player).find(
            (house) =>
                house.parking && flatDistance(player.position, house.parking.position) <= reach,
        ) ?? null
    );
}

// /park beside the car on a home's spot parks it at that home.
dealership.setHomeSpot(
    (player, vehicle) =>
        ownedBy(player).find(
            (house) =>
                house.parking &&
                flatDistance(vehicle.position, house.parking.position) <= PARK_REACH,
        )?.id ?? null,
);

/** Whether the player sits in their own garage car, the one that is out. */
export function inOwnCar(player: Player): boolean {
    const vehicle = player.getVehicle();

    return Boolean(vehicle && dealership.carOut(player)?.vehicle.id === vehicle.id);
}

export function parkHere(player: Player, house: House): void {
    if (!inOwnCar(player)) {
        toast(player, 'Only a car from your garage parks here (/garage).', { tone: 'bad' });

        return;
    }

    if (!dealership.parkAtHome(player, house.id)) {
        toast(player, 'Someone is still sitting in the car.', { tone: 'bad' });

        return;
    }

    toast(
        player,
        `Into the garage of ${title(house)}, as it is, restarts and all. G on this spot brings it out again.`,
        { tone: 'good', seconds: 6 },
    );
}

/** Whether a car of the player's is parked at the home. */
export function hasParked(player: Player, house: House): boolean {
    return dealership.parkedAt(player, house.id).length > 0;
}

/** Brings out the car in the garage slot, or the first one parked at the home. */
export function takeOut(player: Player, house: House, slot: number | null = null): void {
    const parked = dealership.parkedAt(player, house.id);
    const pick = slot === null ? parked[0] : parked.find((car) => car.slot === slot);

    if (!pick) {
        toast(player, 'No car of yours is parked here.', { tone: 'bad' });

        return;
    }

    const problem = blocked(player);

    if (problem) {
        toast(player, problem, { tone: 'bad' });

        return;
    }

    if (!house.parking || !dealership.fromHome(player, pick.slot, house.parking)) {
        toast(player, 'The car could not be brought out. Try again.', { tone: 'bad' });

        return;
    }

    toast(player, 'Your car is at the kerb.', { tone: 'good', seconds: 3 });
}

/** /home car [number]: takes a car out on foot at the spot, by its garage number. */
export function carCommand(player: Player, slot: string | undefined): void {
    const spot =
        playable(player) && player.virtualWorld === MAIN_WORLD && !player.getVehicle()
            ? parkingNear(player, TAKE_OUT_REACH)
            : null;

    if (!spot) {
        toast(player, "Stand on your home's parking spot.", { tone: 'bad' });

        return;
    }

    takeOut(player, spot, slot === undefined ? null : Number(slot) - 1);
}
