// Thirty cash stashes are hidden at the roadside across each map. Each one
// shows only when a player is close, pays once per player and the find is
// remembered between sessions; the last one on a map pays a large bonus.
// Finds are stored as "<map>:<number>".
import * as roads from './lib/roads';
import * as profiles from './lib/profiles';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { events, every } from './lib/lifecycle';
import { toast, markers, feed, name, text, COLORS } from './lib/notify';
import { perMap } from './lib/spots';
import { MAIN_WORLD, distance, flatDistance, playable, formatMoney, type Point } from './lib/util';
import type { MapDefinition } from './lib/maps';

const COUNT = 30;
const MIN_SPACING = 140;
const REACH = 2.2;
const PRIZE = 100;
const FULL_SET_BONUS = 2500;
const XP = 20;
const SHOW_WITHIN = 45;
const TICK_MS = 500;
const SEED = 0x1931;
const MAX_ATTEMPTS = 5000;
// Toward the kerb, still on the road surface so it can always be reached.
const KERB_OFFSET = 3.5;

interface Stash {
    id: string;
    position: Point;
}

// A fixed seed keeps the same spots on every server and every restart.
function seeded(seed: number): () => number {
    let value = seed >>> 0;

    return () => {
        value = (Math.imul(value ^ (value >>> 15), 0x2c1b3c6d) + 0x6d2b79f5) >>> 0;

        return value / 0x100000000;
    };
}

function chooseSpots(map: MapDefinition): Stash[] {
    const graph = roads.forMap(map);
    const random = seeded(SEED);
    const spots: Stash[] = [];

    for (let attempt = 0; spots.length < COUNT && attempt < MAX_ATTEMPTS; attempt++) {
        const { position, direction } = graph.roadPoint(random);
        const spot = {
            x: position.x + direction.z * KERB_OFFSET,
            y: position.y + 0.2,
            z: position.z - direction.x * KERB_OFFSET,
        };

        if (spots.every((other) => flatDistance(other.position, spot) >= MIN_SPACING)) {
            spots.push({ id: `${map.id}:${spots.length + 1}`, position: spot });
        }
    }

    return spots;
}

const currentSpots = perMap<readonly Stash[]>(chooseSpots, []);

// Every find the player has, on any map.
function allFound(player: Player): string[] {
    const stashes: unknown[] = profiles.get(player)?.stashes ?? [];

    return stashes.filter((id): id is string => typeof id === 'string');
}

function found(player: Player): Set<string> {
    const ids = new Set(currentSpots().map((spot) => spot.id));

    return new Set(allFound(player).filter((id) => ids.has(id)));
}

function nearby(player: Player): void {
    const done = found(player);

    markers(
        player,
        'stash',
        currentSpots()
            .filter((spot) => !done.has(spot.id))
            .map((spot) => ({
                id: `s${spot.id}`,
                position: spot.position,
                label: 'CASH',
                color: COLORS.green,
                model: '9money.i3d',
                range: SHOW_WITHIN,
            })),
    );
}

function collect(player: Player, spot: Stash, done: Set<string>): void {
    const total = currentSpots().length;

    done.add(spot.id);
    profiles.setField(player, 'stashes', [...new Set([...allFound(player), spot.id])]);
    player.giveMoney(PRIZE);
    profiles.addXp(player, XP, 'Cash stash');
    nearby(player);

    if (done.size === total) {
        player.giveMoney(FULL_SET_BONUS);
        feed('stash', [
            name(player, COLORS.gold),
            text(` found all ${total} cash stashes · `),
            text(formatMoney(FULL_SET_BONUS), COLORS.gold),
        ]);

        toast(player, `Every stash in the city. ${formatMoney(FULL_SET_BONUS)} bonus.`, {
            title: 'Cash stash',
            tone: 'gold',
        });
    } else {
        toast(player, `Cash stash ${done.size}/${total}: ${formatMoney(PRIZE)}.`, {
            title: 'Cash stash',
            tone: 'good',
            seconds: 3,
        });
    }
}

every(TICK_MS, () => {
    for (const player of World.getPlayers()) {
        if (!playable(player) || player.virtualWorld !== MAIN_WORLD) {
            continue;
        }

        const done = found(player);
        const spot = currentSpots().find(
            (candidate) =>
                !done.has(candidate.id) && distance(player.position, candidate.position) <= REACH,
        );

        if (spot) {
            collect(player, spot, done);
        }
    }
});

commands.register(
    {
        name: 'stashes',
        usage: '/stashes',
        help: 'How many hidden cash stashes you have found.',
        category: 'City',
    },
    (player) => {
        toast(
            player,
            `${found(player).size} of ${currentSpots().length} cash stashes found. They show only up close, at the roadside.`,
        );
    },
);

menu.provide('stashes', (player) => ({ found: found(player).size, total: currentSpots().length }));

events.on('playerSpawn', nearby);
