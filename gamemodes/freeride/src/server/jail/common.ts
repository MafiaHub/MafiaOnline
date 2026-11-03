// What the jail's prisoners and its visitors share: the way back to the
// street, the arrow over the door at the end of the corridor, and the guard
// holding everyone's guns.
import * as maps from '../lib/maps';
import * as prison from '../lib/prison';
import { arrow, COLORS, type Marker } from '../lib/notify';
import type { Loadout } from '../lib/life';
import type { Placement } from '../lib/util';

const EXIT_ARROW_RANGE = 40;

/** Where prisoners and visitors come out on this map. */
export function street(): Placement | null {
    return maps.current()?.jail.release ?? null;
}

export function exitArrow(): Marker {
    return arrow('exit', prison.exit(), 'EXIT', COLORS.green, EXIT_ARROW_RANGE);
}

/** The player as they are, without their guns. */
export function unarmed(player: Player): Loadout {
    return { weapons: [], selected: 0, health: player.health };
}
