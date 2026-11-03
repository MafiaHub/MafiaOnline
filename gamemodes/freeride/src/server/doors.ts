// Free Ride door lab. Stock mission doors register themselves the first time a
// nearby player uses them. The server then owns their target, lock and native
// use state, and replicas restore the same pose for reconnecting players.
//
// Commands: /doors, /door status|open|openback|close|ajar|lock|unlock|enable|disable.
// All actions find a registered door by its actual mission hinge position; no
// assumed city coordinates or frame names are baked into this example.
import * as commands from './lib/commands';
import * as log from './lib/log';
import { events } from './lib/lifecycle';
import { distance } from './lib/util';

const FREE_RIDE_MISSIONS: ReadonlySet<string> = new Set([
    'freeitaly',
    'freeride',
    'freeridenoc',
    'freekrajina',
    'freeride_extended',
]);
const COMMAND_REACH = 4.5;
const LIST_REACH = 18;
const LISTED = 4;
// How far /door ajar opens a door.
const AJAR = 0.35;
const USAGE = '/door status|open|openback|close|ajar|lock|unlock|enable|disable';

function inFreeRide(): boolean {
    return FREE_RIDE_MISSIONS.has(World.getMission());
}

function nearbyDoors(player: Player, radius: number): { door: Door; distance: number }[] {
    return Door.getAll()
        .map((door) => ({ door, distance: distance(player.position, door.position) }))
        .filter((entry) => entry.distance <= radius)
        .sort((a, b) => a.distance - b.distance);
}

function describe(door: Door): string {
    const pose = door.open ? `${Math.round(door.openFraction * 100)}% open` : 'closed';
    const control = door.enabled ? (door.locked ? 'locked' : 'usable') : 'use disabled';

    return `${door.frameName}: ${pose}, ${control}`;
}

function available(player: Player): boolean {
    if (inFreeRide() && player.spawned && player.alive) {
        return true;
    }

    player.sendMessage('Door controls are available on foot in Free Ride.');

    return false;
}

/** What the action did to the door; false when the door refused. */
function act(door: Door, action: string): boolean | null {
    switch (action) {
        case 'status':
            return true;
        // Paired leaves swing apart.
        case 'open':
            return door.openDoor(false, true);
        case 'openback':
            return door.openDoor(true, false);
        case 'close':
            return door.closeDoor();
        case 'ajar':
            return door.setOpenFraction(AJAR, false, true);
        case 'lock':
            door.locked = true;

            return true;
        case 'unlock':
            door.locked = false;

            return true;
        case 'enable':
            door.enabled = true;

            return true;
        case 'disable':
            door.enabled = false;

            return true;
        default:
            return null;
    }
}

commands.register(
    {
        name: 'doors',
        usage: '/doors',
        help: 'List synchronized doors nearby.',
        category: 'Showcase',
    },
    (player) => {
        if (!available(player)) {
            return;
        }

        const nearby = nearbyDoors(player, LIST_REACH);

        if (!nearby.length) {
            player.sendMessage(
                'No synchronized door nearby. Use a stock door once, then try /doors.',
            );

            return;
        }

        for (const { door, distance: gap } of nearby.slice(0, LISTED)) {
            player.sendMessage(`${describe(door)} (${gap.toFixed(1)} m)`);
        }
    },
);

commands.register(
    {
        name: 'door',
        usage: USAGE,
        help: 'Control the nearest synchronized door.',
        category: 'Showcase',
    },
    (player, args) => {
        if (!available(player)) {
            return;
        }

        const nearest = nearbyDoors(player, COMMAND_REACH)[0];

        if (!nearest) {
            player.sendMessage(
                'No synchronized door within reach. Use a stock door once to register it.',
            );

            return;
        }

        const { door } = nearest;
        const action = args[0]?.toLowerCase() ?? 'status';
        const accepted = act(door, action);

        if (accepted === null) {
            player.sendMessage(`Use ${USAGE}.`);

            return;
        }

        player.sendMessage(
            accepted ? describe(door) : `Cannot ${action} ${door.frameName} right now.`,
        );
    },
);

events.on('doorStateChange', (door, player) => {
    if (inFreeRide()) {
        log.info(`Doors: ${player?.nickname ?? 'Script'}: ${describe(door)}`);
    }
});
