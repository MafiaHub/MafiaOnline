// Turf. Eight street corners spread across the city can be held by a crew.
// Members standing on an unheld or rival corner with no other crew around
// take it over in a minute; a held corner pays every online member of its
// crew. Ownership persists between sessions.
import * as store from './lib/store';
import * as roads from './lib/roads';
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { toast, card, markersAll, markers, feed, text, COLORS, type Marker } from './lib/notify';
import { MAIN_WORLD, flatDistance, playable, formatMoney, plural, type Point } from './lib/util';
import { events, every, onMissionChange, onStart } from './lib/lifecycle';
import { reward } from './lib/rewards';
import type { CrewTag } from './lib/nametag';

const TURF_COUNT = 8;
const RADIUS = 45;
const CAPTURE_MS = 60_000;
const TICK_MS = 2000;
const INCOME_EVERY_MS = 3 * 60_000;
const INCOME = 40;
const CAPTURE_XP = 60;
const LETTERS = 'ABCDEFGH';

interface Turf {
    id: string;
    position: Point;
    owner: CrewTag | null;
    /** Time the taking crew has stood on it, in ms. */
    progress: number;
    /** The tag of the crew taking it. */
    by: string | null;
    contested: boolean;
}

/** Owners by map, then by turf letter. Version 1 held stock Lost Heaven's alone. */
type Owners = Record<string, Record<string, CrewTag | null>>;

interface TurfDocument {
    version: number;
    owners?: Owners | Record<string, CrewTag | null>;
}

let owners: Owners = {};
let turfs: Turf[] = [];
let turfMap: string | null = null;
let lastIncome = 0;

// Corners chosen by farthest-point sampling over the map's road crossings,
// so they spread evenly and every one lies on a street.
function chooseCorners(graph: roads.Roads): Turf[] {
    const chosen = [graph.nearestCrossing({ x: 0, y: 0, z: 0 })];

    while (chosen.length < Math.min(TURF_COUNT, graph.crossings.length)) {
        let best = -1;
        let bestGap = -1;

        graph.crossings.forEach((crossing, index) => {
            const gap = Math.min(
                ...chosen.map((other) => flatDistance(crossing, graph.crossings[other])),
            );

            if (gap > bestGap) {
                best = index;
                bestGap = gap;
            }
        });

        chosen.push(best);
    }

    return chosen.map((index, order) => ({
        id: LETTERS[order],
        position: graph.crossings[index],
        owner: null,
        progress: 0,
        by: null,
        contested: false,
    }));
}

function load(): void {
    const stored = store.load<TurfDocument>('turf');

    if (stored) {
        owners =
            stored.version === 1
                ? { freeride: (stored.owners ?? {}) as Record<string, CrewTag | null> }
                : ((stored.owners ?? {}) as Owners);
    }
}

function useMap(): void {
    const map = maps.current();

    turfMap = map?.id ?? null;
    turfs = map ? chooseCorners(roads.forMap(map)) : [];

    for (const turf of turfs) {
        turf.owner = turfMap ? (owners[turfMap]?.[turf.id] ?? null) : null;
    }
}

function save(): void {
    if (!turfMap) {
        return;
    }

    owners[turfMap] = Object.fromEntries(turfs.map((turf) => [turf.id, turf.owner]));
    store.save('turf', { version: 2, owners } satisfies TurfDocument);
}

function items(): Marker[] {
    return turfs.map((turf) => ({
        id: turf.id,
        position: turf.position,
        ring: RADIUS,
        range: 700,
        label: turf.owner ? `TURF ${turf.id} · [${turf.owner.tag}]` : `TURF ${turf.id} · OPEN`,
        color: turf.contested ? COLORS.red : (turf.owner?.color ?? COLORS.muted),
    }));
}

function publish(player: Player | null = null): void {
    if (player) {
        markers(player, 'turf', items());
    } else {
        markersAll('turf', items());
    }
}

function crewOf(player: Player): CrewTag | null | undefined {
    return player.state.get('fr.crew');
}

// Returns whether the turf changed hands or its contest began or ended.
function tickTurf(turf: Turf, street: readonly Player[], onTurf: Set<number>): boolean {
    let changed = false;
    const present = street.filter(
        (player) => flatDistance(player.position, turf.position) <= RADIUS,
    );
    const crews = new Map<string, CrewTag>();

    for (const player of present) {
        const crew = crewOf(player);

        if (crew) {
            crews.set(crew.tag, crew);
        }
    }

    const contested = crews.size > 1;

    if (contested !== turf.contested) {
        turf.contested = contested;
        changed = true;
    }

    const [taker] = crews.size === 1 ? [...crews.values()] : [];

    if (!taker || taker.tag === turf.owner?.tag) {
        if (!contested) {
            turf.progress = 0;
        }
    } else {
        if (turf.by !== taker.tag) {
            turf.by = taker.tag;
            turf.progress = 0;
        }

        turf.progress += TICK_MS;

        if (turf.progress >= CAPTURE_MS) {
            const previous = turf.owner;

            turf.owner = { tag: taker.tag, name: taker.name, color: taker.color };
            turf.progress = 0;
            changed = true;
            save();
            feed('turf', [
                text(`[${taker.tag}]`, taker.color),
                text(previous ? ` took turf ${turf.id} from ` : ` claimed turf ${turf.id}`),
                ...(previous ? [text(`[${previous.tag}]`, previous.color)] : []),
            ]);

            for (const player of present) {
                reward(player, {
                    counters: ['turfCaptures'],
                    xp: CAPTURE_XP,
                    reason: `Took turf ${turf.id}`,
                });
            }
        }
    }

    for (const player of present) {
        const crew = crewOf(player);
        const status = contested
            ? 'Contested'
            : !crew
              ? 'Join a crew to take it'
              : crew.tag === turf.owner?.tag
                ? 'Yours'
                : `Taking over · ${Math.round((turf.progress / CAPTURE_MS) * 100)}%`;

        onTurf.add(player.id);
        card(player, 'turf', {
            title: `Turf ${turf.id}`,
            subtitle: turf.owner
                ? `Held by [${turf.owner.tag}] ${turf.owner.name}`
                : 'Nobody holds this corner',
            rows: [
                ['Status', status],
                ['Pays', `${formatMoney(INCOME)} each, every 3 min`],
            ],
            progress: turf.progress / CAPTURE_MS,
            tone: contested ? 'bad' : 'gold',
        });
    }

    return changed;
}

function payIncome(): void {
    for (const player of World.getPlayers()) {
        const tag = crewOf(player)?.tag;
        const held = tag ? turfs.filter((turf) => turf.owner?.tag === tag).length : 0;

        if (!held || !player.spawned) {
            continue;
        }

        player.giveMoney(INCOME * held);
        toast(player, `Your crew's ${plural(held, 'turf')} paid ${formatMoney(INCOME * held)}.`, {
            title: 'Turf',
            tone: 'good',
            seconds: 4,
        });
    }
}

function tick(): void {
    const now = Date.now();
    const street = World.getPlayers().filter(
        (player) => playable(player) && player.virtualWorld === MAIN_WORLD,
    );
    const onTurf = new Set<number>();
    let changed = false;

    for (const turf of turfs) {
        changed = tickTurf(turf, street, onTurf) || changed;
    }

    for (const player of World.getPlayers()) {
        if (!onTurf.has(player.id)) {
            card(player, 'turf', null);
        }
    }

    if (changed) {
        publish();
    }

    if (now - lastIncome >= INCOME_EVERY_MS) {
        lastIncome = now;
        payIncome();
    }
}

commands.register(
    { name: 'turf', usage: '/turf', help: "Who holds the city's corners.", category: 'Crew' },
    (player) => {
        player.sendMessage(
            turfs
                .map((turf) => `${turf.id}: ${turf.owner ? `[${turf.owner.tag}]` : 'open'}`)
                .join('  '),
            0xd9cdb4,
        );

        markers(player, 'turf', items());
    },
);

menu.provide('turf', () =>
    turfs.map((turf) => ({ id: turf.id, owner: turf.owner, contested: turf.contested })),
);

onStart(() => {
    load();
    useMap();
    lastIncome = Date.now();
    publish();
});

every(TICK_MS, tick);

events.on('playerSpawn', (player) => publish(player));
onMissionChange(useMap);
