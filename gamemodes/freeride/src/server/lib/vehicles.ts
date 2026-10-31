// Vehicles the gamemode spawns and the checks every module makes on them.
import type { Point } from './util';

/** Whether anyone sits in the vehicle. */
export function occupied(vehicle: Vehicle): boolean {
    return vehicle.getOccupants().some((occupant) => occupant !== null);
}

/** Whether nobody but `except` (if given) sits in the vehicle. */
export function emptyBut(vehicle: Vehicle, except: Player | null = null): boolean {
    return vehicle
        .getOccupants()
        .every((occupant) => occupant === null || occupant.id === except?.id);
}

export interface SpawnOptions {
    /** The player who simulates it first, usually the one about to drive. */
    controller?: Player;
    /** Shown as `fr.owner` and keeps others from taking it (dealership locks). */
    owner?: Player;
    /** City-map blip colour (0xRRGGBB); none when omitted. */
    radar?: number;
    /** Starts with the engine off, as a parked car. */
    engineOff?: boolean;
    /** False leaves the tank as the model has it (a wreck); full when omitted. */
    fullTank?: boolean;
    /** Extra replicated state, set before anyone sees it. */
    state?: Record<string, unknown>;
    /** A condition saved with Vehicle.saveState (a garage car), restored before anything else. */
    condition?: string | null;
}

/** A vehicle the gamemode hands out: full tank, tagged, optionally on the radar. */
export function spawnVehicle(
    model: string,
    position: Point,
    heading: number,
    options: SpawnOptions = {},
): Vehicle | null {
    // The native counts an explicit undefined as a controller and rejects it.
    const vehicle = options.controller
        ? Vehicle.spawn(model, position, heading, options.controller)
        : Vehicle.spawn(model, position, heading);

    if (!vehicle) {
        return null;
    }

    // Before the first native report, which would otherwise set it.
    if (options.condition) {
        vehicle.restoreState(options.condition);
    }

    if (options.engineOff) {
        vehicle.setEngine(false);
    }

    if (options.fullTank !== false) {
        vehicle.state.set('fr.fullTank', true);
    }

    if (options.owner) {
        vehicle.state.set('fr.owner', options.owner.nickname);
    }

    for (const [key, value] of Object.entries(options.state ?? {})) {
        vehicle.state.set(key, value);
    }

    if (options.radar !== undefined) {
        vehicle.setRadarMarker(true, options.radar);
    }

    return vehicle;
}

/** Destroys the vehicle after `delayMs` if nobody sits in it by then. */
export function destroyWhenEmpty(vehicleId: number | null, delayMs = 0): void {
    if (vehicleId === null) {
        return;
    }

    setTimeout(() => {
        const vehicle = World.getVehicles().find((candidate) => candidate.id === vehicleId);

        if (vehicle && !occupied(vehicle)) {
            vehicle.destroy();
        }
    }, delayMs);
}
