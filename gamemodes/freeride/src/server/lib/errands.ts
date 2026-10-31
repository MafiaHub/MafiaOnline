// What the jobs that send a player somewhere share: a destination on the
// compass and the map, a vehicle stopped at it, the countdowns of a run and
// the job car beside the player. Taxi fares, the export list, bootleg runs,
// the farm's loads, hold-ups and police takedowns are built from these; they
// pay with lib/rewards.ts.
import { card, compass, markers, type Marker } from './notify';
import { ahead, flatDistance, headingOf, speedOf, type Point } from './util';
import { spawnVehicle, type SpawnOptions } from './vehicles';

/** A place the player is sent to: a beacon over it, first among the markers. */
export function beacon(id: string, position: Point, label: string, color: string): Marker {
    return { id, position, label, color, beacon: true, primary: true };
}

/** Points the job's compass at the destination and marks it; null takes both away. */
export function lead(
    player: Player,
    hud: string,
    priority: number,
    destination: Marker | null,
): void {
    compass(player, hud, priority, destination ? { position: destination.position } : null);
    markers(player, hud, destination ? [destination] : []);
}

/** Takes the job's compass, markers and card off the player's HUD. */
export function clearHud(player: Player, hud: string, priority: number): void {
    compass(player, hud, priority, null);
    markers(player, hud, []);
    card(player, hud, null);
}

/** Whether the vehicle stands (slower than `speed`, m/s) within `radius` of the point. */
export function stoppedAt(vehicle: Vehicle, point: Point, radius: number, speed: number): boolean {
    return flatDistance(vehicle.position, point) <= radius && speedOf(vehicle) < speed;
}

/** A stretch of a run with an end: loading, holding a counter, a delivery deadline. */
export class Countdown {
    readonly endsAt: number;

    constructor(
        readonly ms: number,
        readonly startedAt = Date.now(),
    ) {
        this.endsAt = startedAt + ms;
    }

    /** What is left of it; negative once it is over. */
    left(now = Date.now()): number {
        return this.endsAt - now;
    }

    /** How much of it has passed, up to 1. */
    progress(now = Date.now()): number {
        return Math.min(1, (now - this.startedAt) / this.ms);
    }

    over(now = Date.now()): boolean {
        return now >= this.endsAt;
    }
}

/**
 * A job's vehicle beside the player, or beside the car they sit in (which
 * they leave), with them at its wheel. Null when it could not be spawned or
 * they could not be seated; nothing is left standing then.
 */
export function spawnBeside(
    player: Player,
    model: string,
    options: SpawnOptions = {},
): Vehicle | null {
    const current = player.getVehicle();
    const heading = headingOf(current?.rotation ?? player.rotation);
    // 3.5 m to the side, a little up so it drops onto its wheels.
    const position = ahead(current?.position ?? player.position, heading + Math.PI / 2, 3.5, 0.3);

    if (current) {
        player.removeFromVehicle();
    }

    const vehicle = spawnVehicle(model, position, heading, { controller: player, ...options });

    if (!vehicle || !player.putInVehicle(vehicle, 0)) {
        vehicle?.destroy();

        return null;
    }

    return vehicle;
}
