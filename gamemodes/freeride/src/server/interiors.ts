// Entrances into the city's buildings. A glowing arrow marks each door; a
// player who walks into it fades out and arrives inside, and the arrow by the
// arrival point leads back out. Public interiors share the street world. A
// safehouse is private: each crew, or each crewless player, gets its own copy
// in a virtual world, and it patches wounds and tops up ammunition.
//
// No interior positions ship with the gamemode, because none can be verified
// outside the game. Record them in-game in development mode: stand on the
// street at the door, "/entrance add <name> [public|safehouse]", walk to the
// arrival point inside, "/entrance inside". They are saved in
// the gamemode's database (lib/store.ts), each with its map, and live at once.
import * as store from './lib/store';
import * as life from './lib/life';
import * as devmode from './lib/devmode';
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { events, every, onStart, onStop } from './lib/lifecycle';
import type { CrewTag } from './lib/nametag';
import { toast, markers, markersAll, arrow, COLORS, type Marker } from './lib/notify';
import { PlayerMap } from './lib/players';
import { WalkIn } from './lib/spots';
import { Transfers } from './lib/transfer';
import { MAIN_WORLD, distance, playable, plain, headingOf, type Placement } from './lib/util';

const TICK_MS = 250;
const TRIGGER_RADIUS = 1.4;
const REARM_DISTANCE = 3;
const MARKER_RANGE = 60;
const FIRST_SAFEHOUSE_WORLD = 0x30000000;
const BLOCKING_ACTIVITIES = new Set<string>(['race', 'race-lobby', 'duel']);

type Kind = 'public' | 'safehouse';

/** An interior recorded in-game with /entrance. */
interface RecordedInterior {
    id: number;
    name: string;
    kind: Kind;
    outside: Placement;
    inside: Placement;
    /** The map it is on; stock Lost Heaven's when missing (recorded before maps existed). */
    mission?: string;
    author: string;
    created: number;
}

/** A building on the loaded map: built into the map (lib/maps.ts) or recorded. */
export type Interior = maps.MapInterior | RecordedInterior;

type Side = 'inside' | 'outside';

let interiors: RecordedInterior[] = [];
// player id -> the interior they are in
const inside = new PlayerMap<Interior['id']>();
// player id -> the virtual world the interior put them in
const worlds = new PlayerMap<number>();
// Each arrow ("<interior id>:<side>") moves a player once, and again after
// they step away.
const arrows = new WalkIn(TRIGGER_RADIUS, REARM_DISTANCE);
const transfers = new Transfers();
const recording = new PlayerMap<Omit<RecordedInterior, 'id' | 'inside' | 'author' | 'created'>>();
let nextId = 1;

function load(): void {
    interiors =
        store.load<{ version: number; interiors: RecordedInterior[] }>('interiors')?.interiors ??
        [];

    nextId = interiors.reduce((highest, interior) => Math.max(highest, interior.id), 0) + 1;
}

function save(): void {
    store.save('interiors', { version: 1, interiors });
}

function label(interior: Interior): string {
    return interior.kind === 'safehouse'
        ? `${interior.name.toUpperCase()} · SAFEHOUSE`
        : interior.name.toUpperCase();
}

function missionOf(interior: RecordedInterior): string {
    return interior.mission ?? 'freeride';
}

// Interiors of the loaded map; ones recorded before maps existed are stock Lost Heaven's.
// Built into the map (lib/maps.ts) first, then the recorded ones.
function here(): Interior[] {
    const map = maps.current();

    return [
        ...(map?.interiors ?? []),
        ...interiors.filter((interior) => missionOf(interior) === map?.id),
    ];
}

function publishEntrances(player: Player | null = null): void {
    const items: Marker[] = here().map((interior) =>
        arrow(
            `in${interior.id}`,
            interior.outside.position,
            label(interior),
            interior.kind === 'safehouse' ? COLORS.blue : COLORS.gold,
            MARKER_RANGE,
        ),
    );

    if (player) {
        markers(player, 'entrances', items);
    } else {
        markersAll('entrances', items);
    }
}

// One private copy per crew, or per player without one.
function safehouseWorld(player: Player): number {
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');
    const tag = crew?.tag;

    if (!tag) {
        return FIRST_SAFEHOUSE_WORLD + player.id;
    }

    let hash = 0;

    for (const character of tag) {
        hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
    }

    return FIRST_SAFEHOUSE_WORLD + 0x8000000 + (hash % 0x7000000);
}

function blocked(player: Player): string | null {
    const current = life.activity(player);

    if (current && BLOCKING_ACTIVITIES.has(current.kind)) {
        return life.busyMessage(player);
    }

    // Moving through a door starts a new native life, which would shake off the police.
    if ((Police.getRecord(player)?.level ?? 0) > 0) {
        return 'The door stays shut while the police are after you.';
    }

    return null;
}

// Fade out, move, fade in; the move is skipped if the life ended meanwhile,
// and a move refused (a client still loading the mission) leaves the player
// where and in the world they were.
function move(player: Player, pose: Placement, world: number, then: () => void): void {
    transfers.move(player, pose, world, {
        then: (moved) => {
            if (moved) {
                then();
            }
        },
    });
}

function enter(player: Player, interior: Interior): void {
    // A room built from map objects (interior.open) may not stand yet.
    const open = 'open' in interior ? interior.open : undefined;
    const problem = open && !open() ? 'The door is locked.' : blocked(player);

    if (problem) {
        toast(player, problem, { tone: 'bad', seconds: 3 });
        arrows.disarm(player, `${interior.id}:outside`);

        return;
    }

    // A public interior shares the street world unless it is built apart.
    const world =
        interior.kind === 'safehouse'
            ? safehouseWorld(player)
            : (('world' in interior ? interior.world : undefined) ?? MAIN_WORLD);

    move(player, interior.inside, world, () => {
        inside.set(player.id, interior.id);
        worlds.set(player.id, world);
        arrows.disarm(player, `${interior.id}:inside`);
        markers(player, 'exit', [
            arrow('exit', interior.inside.position, 'EXIT', COLORS.green, 30),
        ]);

        if (interior.kind === 'safehouse') {
            player.setHealth(100);

            for (const weapon of player.getWeapons()) {
                if (weapon.kind === 'firearm') {
                    player.giveAmmo(weapon.weaponId, weapon.magazine * 2);
                }
            }

            toast(player, 'Safe for now. Wounds dressed, magazines filled.', {
                title: interior.name,
                tone: 'good',
            });
        }
    });
}

function leave(player: Player, interior: Interior): void {
    move(player, interior.outside, MAIN_WORLD, () => {
        inside.delete(player.id);
        worlds.delete(player.id);
        arrows.disarm(player, `${interior.id}:outside`);
        markers(player, 'exit', []);
    });
}

function tick(): void {
    for (const player of World.getPlayers()) {
        if (!playable(player) || player.getVehicle() || transfers.isMoving(player)) {
            continue;
        }

        const current = insideOf(player);
        const checks: [Interior, Side][] = current
            ? [[current, 'inside']]
            : player.virtualWorld === MAIN_WORLD
              ? here().map((interior) => [interior, 'outside'])
              : [];

        for (const [interior, side] of checks) {
            if (arrows.reached(player, interior[side].position, `${interior.id}:${side}`)) {
                if (side === 'outside') {
                    enter(player, interior);
                } else {
                    leave(player, interior);
                }

                break;
            }
        }
    }
}

const subcommands = new Map<string, (player: Player, args: string[]) => void>([
    [
        'add',
        (player, args) => {
            const last = args[args.length - 1];
            const kind: Kind = last === 'public' || last === 'safehouse' ? last : 'public';

            if (last === 'public' || last === 'safehouse') {
                args.pop();
            }

            const name = args.join(' ').trim();

            if (!/^[A-Za-z0-9 '.&-]{3,28}$/.test(name)) {
                toast(
                    player,
                    'Use /entrance add <name> [public|safehouse], standing on the street at the door.',
                    { tone: 'bad' },
                );

                return;
            }

            recording.set(player.id, {
                name,
                kind,
                outside: {
                    position: plain(player.position),
                    heading: headingOf(player.rotation) + Math.PI,
                },
            });

            toast(
                player,
                `Door recorded for ${name}. Walk to where players should arrive inside, face the way they should look, and type /entrance inside.`,
                { seconds: 10 },
            );
        },
    ],
    [
        'inside',
        (player) => {
            const record = recording.get(player.id);

            if (!record) {
                toast(player, 'Record the door first with /entrance add <name>.', { tone: 'bad' });

                return;
            }

            if (distance(player.position, record.outside.position) < REARM_DISTANCE * 2) {
                toast(player, 'The arrival point must be away from the door marker.', {
                    tone: 'bad',
                });

                return;
            }

            recording.delete(player.id);
            const interior: RecordedInterior = {
                id: nextId++,
                ...record,
                inside: { position: plain(player.position), heading: headingOf(player.rotation) },
                mission: maps.current()?.id ?? 'freeride',
                author: player.nickname,
                created: Date.now(),
            };

            interiors.push(interior);
            save();
            publishEntrances();
            toast(player, `${interior.name} is open (${interior.kind}).`, { tone: 'good' });
        },
    ],
    [
        'delete',
        (player, args) => {
            const name = args.join(' ').trim().toLowerCase();
            const before = interiors.length;
            const doomed = new Set(
                interiors.filter(
                    (interior) =>
                        interior.name.toLowerCase() === name &&
                        missionOf(interior) === maps.current()?.id,
                ),
            );

            interiors = interiors.filter((interior) => !doomed.has(interior));

            if (interiors.length === before) {
                toast(player, 'No interior by that name.', { tone: 'bad' });

                return;
            }

            save();
            publishEntrances();
            toast(player, 'Interior removed.');
        },
    ],
    ['list', listInteriors],
]);

function listInteriors(player: Player): void {
    if (!here().length) {
        player.sendMessage('No interiors on this map yet.', 0xd9cdb4);
    }

    for (const interior of here()) {
        player.sendMessage(
            `${interior.name} (${interior.kind}) · ${Math.round(distance(player.position, interior.outside.position))} m away`,
            0xd9cdb4,
        );
    }
}

commands.register(
    {
        name: 'entrance',
        usage: '/entrance add <name> [public|safehouse] | inside | delete <name> | list',
        help: 'Record building entrances (development mode).',
        category: 'Dev',
        hidden: true,
    },
    (player, [action, ...args]) => {
        if (!devmode.isOn()) {
            toast(player, 'Entrances are recorded in development mode.', { tone: 'bad' });

            return;
        }

        if (!playable(player) || player.getVehicle()) {
            return;
        }

        (subcommands.get(action ?? '') ?? listInteriors)(player, args);
    },
);

commands.register(
    {
        name: 'interiors',
        usage: '/interiors',
        help: 'List the buildings you can walk into.',
        category: 'City',
    },
    listInteriors,
);

menu.provide('interiors', (player) =>
    here().map((interior) => ({
        name: interior.name,
        kind: interior.kind,
        distance: Math.round(distance(player.position, interior.outside.position)),
    })),
);

onStart(() => {
    load();
    publishEntrances();
    every(TICK_MS, tick);
});

// Out the door before the rooms go: a room built apart stands under the
// fields, where the street world has no floor.
onStop(() => {
    for (const player of World.getPlayers()) {
        const interior = insideOf(player);

        if (!interior) {
            continue;
        }

        if (player.virtualWorld === worlds.get(player.id)) {
            player.setVirtualWorld(MAIN_WORLD);
        }

        if (player.alive) {
            life.teleport(player, interior.outside.position, interior.outside.heading);
        }
    }
});

events.on('playerSpawn', (player) => {
    if (!life.isTransit(player)) {
        publishEntrances(player);
    }
});

// A death inside sends the player back to the street world for the respawn,
// whatever they were busy with (a job keeps going).
events.on('playerDeath', (player) => {
    if (!inside.has(player.id)) {
        return;
    }

    inside.delete(player.id);
    markers(player, 'exit', []);

    if (player.virtualWorld === worlds.get(player.id)) {
        player.setVirtualWorld(MAIN_WORLD);
    }

    worlds.delete(player.id);
});

// The mission change emptied the rooms; nobody stays in their world.
events.on('missionChange', () => {
    for (const player of World.getPlayers()) {
        if (inside.has(player.id) && player.virtualWorld === worlds.get(player.id)) {
            player.setVirtualWorld(MAIN_WORLD);
        }
    }

    inside.clear();
    worlds.clear();
    transfers.clear();
});

/** The interior the player is in, or null on the street. */
export function insideOf(player: Player): Interior | null {
    return here().find((interior) => interior.id === inside.get(player.id)) ?? null;
}

// Another module moved the player out (server/bars after a blackout, the
// jail): the interior no longer holds them.
export function release(player: Player): void {
    if (!inside.delete(player.id)) {
        return;
    }

    worlds.delete(player.id);
    transfers.forget(player);
    markers(player, 'exit', []);
}
