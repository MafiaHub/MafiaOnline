// Speed traps on each map's six longest roads. Driving through one records
// the car's speed against the player's own best and the server record;
// beating the record is announced and pays. Records and bests are keyed
// "<map>:<trap>"; plain numbers are stock Lost Heaven's from before maps.
import * as store from './lib/store';
import * as roads from './lib/roads';
import * as profiles from './lib/profiles';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { events, every, onStart } from './lib/lifecycle';
import { toast, markersAll, markers, feed, name, text, COLORS, type Marker } from './lib/notify';
import { PlayerMap } from './lib/players';
import { perMap } from './lib/spots';
import { MAIN_WORLD, flatDistance, playable, speedOf, formatMoney, type Point } from './lib/util';

const TRAP_COUNT = 6;
const RADIUS = 14;
const REPEAT_MS = 10_000;
const MIN_KMH = 40;
const RECORD_PRIZE = 150;
const TICK_MS = 150;
const MARKER_RANGE = 250;

interface Trap {
    id: number;
    key: string;
    position: Point;
}

interface SpeedRecord {
    kmh: number;
    name: string;
}

interface SpeedTrapsDocument {
    version: 1;
    records?: Record<string, SpeedRecord>;
}

// The middle of each of the map's longest roads.
const currentTraps = perMap<readonly Trap[]>(
    (map) =>
        roads
            .forMap(map)
            .lines.slice()
            .sort((a, b) => b.length - a.length)
            .slice(0, TRAP_COUNT)
            .map((road, index) => ({
                id: index + 1,
                key: `${map.id}:${index + 1}`,
                position: roads.pointAlong(road.line, roads.lineLength(road.line) / 2).position,
            })),
    [],
);

function migrate<T>(table: Record<string, T> | null | undefined): Record<string, T> {
    return Object.fromEntries(
        Object.entries(table ?? {}).map(([key, value]) => [
            /^\d+$/.test(key) ? `freeride:${key}` : key,
            value,
        ]),
    );
}

let records: Record<string, SpeedRecord> = {};
// When each player last went through each trap, by trap key.
const lastHit = new PlayerMap<Map<string, number>>();

function load(): void {
    const stored = store.load<SpeedTrapsDocument>('speedtraps');

    if (stored) {
        records = migrate(stored.records);
    }
}

function save(): void {
    store.save('speedtraps', { version: 1, records });
}

function items(): Marker[] {
    return currentTraps().map((trap) => ({
        id: `t${trap.id}`,
        position: trap.position,
        range: MARKER_RANGE,
        color: COLORS.blue,
        label: records[trap.key]
            ? `SPEED TRAP ${trap.id} · ${records[trap.key].kmh} KM/H`
            : `SPEED TRAP ${trap.id}`,
    }));
}

function bests(player: Player): Record<string, number> {
    return migrate(profiles.get(player)?.speedBest);
}

function hitsOf(player: Player): Map<string, number> {
    let hits = lastHit.get(player.id);

    if (!hits) {
        hits = new Map();
        lastHit.set(player.id, hits);
    }

    return hits;
}

function clock(player: Player, trap: Trap, kmh: number): void {
    const best = bests(player);
    const personal = kmh > (best[trap.key] ?? 0);

    if (personal) {
        profiles.setField(player, 'speedBest', { ...best, [trap.key]: kmh });
    }

    const record = records[trap.key];

    if (!record || kmh > record.kmh) {
        records[trap.key] = { kmh, name: player.nickname };
        save();
        markersAll('speedtraps', items());
        player.giveMoney(RECORD_PRIZE);
        feed('record', [
            name(player, COLORS.gold),
            text(` set the speed record at trap ${trap.id} · `),
            text(`${kmh} km/h`, COLORS.gold),
        ]);

        toast(player, `${kmh} km/h, a new record. ${formatMoney(RECORD_PRIZE)}.`, {
            title: `Speed trap ${trap.id}`,
            tone: 'gold',
            seconds: 4,
        });

        return;
    }

    toast(
        player,
        `${kmh} km/h${personal ? ' · your best' : ` · best ${best[trap.key]}`} · record ${record.kmh} by ${record.name}`,
        { title: `Speed trap ${trap.id}`, seconds: 3 },
    );
}

every(TICK_MS, () => {
    const now = Date.now();

    for (const player of World.getPlayers()) {
        const car =
            playable(player) && player.virtualWorld === MAIN_WORLD && player.getSeat() === 0
                ? player.getVehicle()
                : null;

        if (!car) {
            continue;
        }

        for (const trap of currentTraps()) {
            const hits = hitsOf(player);

            if (
                flatDistance(car.position, trap.position) > RADIUS ||
                now - (hits.get(trap.key) ?? 0) < REPEAT_MS
            ) {
                continue;
            }

            const kmh = Math.round(speedOf(car) * 3.6);

            if (kmh < MIN_KMH) {
                continue;
            }

            hits.set(trap.key, now);
            clock(player, trap, kmh);
        }
    }
});

commands.register(
    {
        name: 'speedtraps',
        aliases: ['traps'],
        usage: '/speedtraps',
        help: 'Speed trap records and your bests.',
        category: 'Races',
    },
    (player) => {
        const best = bests(player);

        player.sendMessage(
            currentTraps()
                .map((trap) => {
                    const record = records[trap.key];

                    return `#${trap.id}: ${record ? `${record.kmh} (${record.name})` : '-'}, you ${best[trap.key] ?? '-'}`;
                })
                .join('  '),
            0xd9cdb4,
        );

        markers(player, 'speedtraps', items());
    },
);

menu.provide('speedtraps', (player) => {
    const best = bests(player);

    return currentTraps().map((trap) => ({
        id: trap.id,
        record: records[trap.key] ?? null,
        best: best[trap.key] ?? null,
    }));
});

onStart(() => {
    load();
    markersAll('speedtraps', items());
});

events.on('playerSpawn', (player) => markers(player, 'speedtraps', items()));
