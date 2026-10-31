// Which map Free Ride plays. At start the server moves from stock Free Ride
// (or its night map) to the default map, Lost Heaven Extended unless
// FREERIDE_MAP names another; a map that is not installed is skipped.
// Players can vote for another map; a majority of those in town switches it
// after a short warning.
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import * as log from './lib/log';
import { toast, toastAll, feed, name, text, COLORS } from './lib/notify';
import { every, onStart } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { Delay } from './lib/timers';

const SWITCH_WARNING_MS = 15_000;
const SWITCH_COOLDOWN_MS = 5 * 60_000;
const CHECK_MS = 60_000;

const votes = new PlayerMap<maps.MapId>();
const unavailable = new Set<string>();
const pending = new Delay();
let lastSwitch = 0;

// Only a voted change starts the cooldown; the startup move to the default does not.
function change(id: string, voted = false): boolean {
    if (World.changeMission(id)) {
        if (voted) {
            lastSwitch = Date.now();
        }

        votes.clear();

        return true;
    }

    unavailable.add(id);
    log.warn(`Map ${id} is not installed on this server`);

    return false;
}

// At start stock Free Ride gives way to the default map; the night map does
// so whenever it loads, since Free Ride plays by day.
function settle(starting: boolean): void {
    const mission = World.getMission();

    if (mission !== maps.NIGHT_MAP && !(starting && mission === 'freeride')) {
        return;
    }

    if (
        maps.DEFAULT_MAP !== mission &&
        !unavailable.has(maps.DEFAULT_MAP) &&
        maps.ids().some((id) => id === maps.DEFAULT_MAP) &&
        change(maps.DEFAULT_MAP)
    ) {
        return;
    }

    if (mission === maps.NIGHT_MAP) {
        change('freeride');
    }
}

function find(query: string): maps.MapDefinition | null {
    const wanted = query.trim().toLowerCase();
    const all = Object.values(maps.MAPS);

    return (
        all.find((map) => map.id === wanted || map.title.toLowerCase() === wanted) ??
        all.find((map) => map.title.toLowerCase().includes(wanted) || map.id.includes(wanted)) ??
        null
    );
}

function needed(): number {
    return Math.floor(World.getPlayers().filter((player) => player.spawned).length / 2) + 1;
}

function tally(): Map<maps.MapId, number> {
    const counts = new Map<maps.MapId, number>();

    for (const id of votes.values()) {
        counts.set(id, (counts.get(id) ?? 0) + 1);
    }

    return counts;
}

function consider(): void {
    if (pending.pending) {
        return;
    }

    const current = maps.current()?.id;

    for (const [id, count] of tally()) {
        if (id === current || count < needed()) {
            continue;
        }

        const { title } = maps.MAPS[id];
        const wait = lastSwitch + SWITCH_COOLDOWN_MS - Date.now();

        if (wait > 0) {
            toastAll(
                `${title} has the votes; the city can change again in ${Math.ceil(wait / 60_000)} min.`,
                { title: 'Map vote' },
            );

            return;
        }

        pending.start(SWITCH_WARNING_MS, () => {
            if (!change(id, true)) {
                toastAll(`${title} is not installed on this server.`, {
                    title: 'Map vote',
                    tone: 'bad',
                });
            }
        });

        toastAll(`The vote is in: ${title} in ${SWITCH_WARNING_MS / 1000} seconds.`, {
            title: 'Map vote',
            tone: 'gold',
            seconds: 10,
        });

        feed('map', [text('Next map: ', COLORS.muted), text(title, COLORS.gold)]);

        return;
    }
}

commands.register(
    {
        name: 'map',
        aliases: ['votemap', 'maps'],
        usage: '/map [name]',
        help: 'Vote for the map the city plays.',
        category: 'City',
    },
    (player, args) => {
        if (!args.length) {
            const counts = tally();
            const currentId = maps.current()?.id;
            const list = Object.values(maps.MAPS).map(
                (map) =>
                    `${map.title}${map.id === currentId ? ' (now)' : ''}` +
                    `${unavailable.has(map.id) ? ' (not installed)' : ''}: ${counts.get(map.id) ?? 0}`,
            );

            player.sendMessage(
                `Maps · ${list.join(' · ')} · ${needed()} votes switch. /map <name> to vote.`,
                0xd9cdb4,
            );

            return;
        }

        const map = find(args.join(' '));

        if (!map || unavailable.has(map.id)) {
            toast(
                player,
                map
                    ? `${map.title} is not installed on this server.`
                    : 'No such map. /map lists them.',
                { tone: 'bad' },
            );

            return;
        }

        votes.set(player.id, map.id);
        feed('map', [
            name(player, COLORS.gold),
            text(' voted for '),
            text(map.title, COLORS.white),
        ]);

        consider();
    },
);

menu.provide('maps', (player) => {
    const counts = tally();

    return {
        current: maps.current()?.id ?? null,
        needed: needed(),
        maps: Object.values(maps.MAPS).map((map) => ({
            id: map.id,
            title: map.title,
            votes: counts.get(map.id) ?? 0,
            yours: votes.get(player.id) === map.id,
            available: !unavailable.has(map.id),
        })),
    };
});

onStart(() => settle(true));
every(CHECK_MS, () => settle(false));
