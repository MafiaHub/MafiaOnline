// Bars: rooms with a barman behind the counter, drinkers at the tables and
// a drink menu at the counter (N/B, like the shop counters).
//
// Lost Heaven Extended has the real thing. The mod places Salieri's bar of
// the story missions (models/salierybar.4ds) where the stock city has only
// its front, with working doors: walk in through the front door or the side
// door off the alley. It also places Morello's bar (models/morellobar.4ds),
// whose door is a fixed mesh: an arrow on the pavement leads in
// (lib/maps.ts). Stock Lost Heaven has no rooms behind Salieri's door, so
// its arrow leads to a back-room bar built from stock models
// (lib/bar_room.ts).
//
// venues.ts holds the bars and their drinks, staff.ts the people in them.
import * as life from '../lib/life';
import * as maps from '../lib/maps';
import * as barRoom from '../lib/bar_room';
import * as commands from '../lib/commands';
import { events, every, onStart, onStop } from '../lib/lifecycle';
import { npcsAvailable } from '../lib/npcs';
import { toast, compass, markersAll, markers, COLORS, type Marker } from '../lib/notify';
import { fields } from '../lib/payload';
import { PlayerMap, PlayerSet } from '../lib/players';
import {
    distance,
    flatDistance,
    emit,
    onClient,
    formatMoney,
    type Placement,
    type Point,
} from '../lib/util';
import * as interiors from '../interiors';
import * as drunk from '../drunk';
import { barmanIn, cheers, forgetVenues, serve, standDown, tickStaff } from './staff';
import {
    barsHere,
    contains,
    DRINKS,
    interiorId,
    ROUND_PER_HEAD,
    type Bar,
    type Drink,
} from './venues';

export { DRINKS, ROUND_PER_HEAD, type Bar };

const TICK_MS = 500;
const PURCHASE_COOLDOWN_MS = 2500;
const LABEL_RANGE = 40;
const COMPASS_PRIORITY = 45;

let current: Bar[] = [];
const lastPurchase = new PlayerMap<number>();
// The bar each player was last in, for a blackout's wake.
const lastBar = new PlayerMap<Bar['id']>();
// Players whose compass /bar pointed at a bar.
const pointed = new PlayerSet();
// Bars the fire brigade is working in (firefighters): no staff, no drinks.
const closed = new Set<Bar['id']>();

/** The bars of the loaded map. */
export function bars(): Bar[] {
    return current;
}

export function barOf(player: Player | null | undefined): Bar | null {
    if (
        !player?.spawned ||
        !player.alive ||
        player.missionGeneration !== World.getMissionGeneration()
    ) {
        return null;
    }

    return (
        current.find(
            (bar) => player.virtualWorld === bar.world && contains(bar, player.position),
        ) ?? null
    );
}

function guests(bar: Bar): Player[] {
    return World.getPlayers().filter((player) => barOf(player) === bar);
}

export function isClosed(barId: Bar['id']): boolean {
    return closed.has(barId);
}

function tick(): void {
    const now = Date.now();

    for (const bar of current) {
        // A burning bar's people are the fire brigade's to get out.
        tickStaff(bar, guests(bar).length > 0 && !closed.has(bar.id), now);
    }

    for (const player of World.getPlayers()) {
        const bar = barOf(player);

        if (!bar) {
            continue;
        }

        lastBar.set(player.id, bar.id);

        if (pointed.delete(player.id)) {
            compass(player, 'bar', COMPASS_PRIORITY, null);
        }
    }
}

function publicBar(bar: Bar): Record<string, unknown> {
    return {
        id: bar.id,
        name: bar.name,
        tagline: bar.tagline,
        world: bar.world,
        counter: bar.counter,
        drinks: DRINKS.map(({ id, name, price, note }) => ({ id, name, price, note })),
    };
}

function sendState(player: Player): void {
    emit(player, 'fr:bars:state', {
        generation: World.getMissionGeneration(),
        bars: current.map(publicBar),
    });
}

function publishDoors(player: Player | null = null): void {
    const items: Marker[] = current.flatMap((bar) =>
        bar.door
            ? [
                  {
                      id: `bar-${bar.id}`,
                      position: bar.door,
                      label: bar.name.toUpperCase(),
                      color: COLORS.gold,
                      range: LABEL_RANGE,
                  },
              ]
            : [],
    );

    if (player) {
        markers(player, 'bars', items);
    } else {
        markersAll('bars', items);
    }
}

// A round someone else buys does not black a guest out (and cost them cash).
function drink(player: Player, entry: Drink, treated = false): number {
    const health = Math.min(100, player.health + entry.health);

    if (health > player.health) {
        player.setHealth(health);
    }

    return drunk.add(player, entry.strength, treated ? drunk.MAX_LEVEL - 1 : drunk.MAX_LEVEL);
}

function buy(player: Player, request: unknown): void {
    if (
        !World.isReady() ||
        fields(request).generation !== World.getMissionGeneration() ||
        drunk.isBlackedOut(player)
    ) {
        return;
    }

    const bar = barOf(player);
    const entry = DRINKS.find((candidate) => candidate.id === fields(request).drinkId);

    if (
        !bar ||
        bar.id !== fields(request).barId ||
        !entry ||
        player.getVehicle() ||
        flatDistance(player.position, bar.counter.position) > bar.counter.reach ||
        Math.abs(player.position.y - bar.counter.position.y) > 1.5
    ) {
        return;
    }

    const now = Date.now();

    if (now - (lastPurchase.get(player.id) ?? 0) < PURCHASE_COOLDOWN_MS) {
        return;
    }

    lastPurchase.set(player.id, now);

    if (closed.has(bar.id)) {
        toast(player, 'The bar is on fire. Get out!', { tone: 'bad' });

        return;
    }

    if (npcsAvailable() && !barmanIn(bar)) {
        toast(player, 'Nobody is behind the counter right now.', { tone: 'bad' });

        return;
    }

    const company = guests(bar);
    const price = entry.price ?? ROUND_PER_HEAD * company.length;

    if (!player.trySpendMoney(price)) {
        toast(player, `That is ${formatMoney(price)}; you have ${formatMoney(player.money)}.`, {
            tone: 'bad',
        });

        return;
    }

    serve(bar, player);

    if (entry.id === 'round') {
        for (const guest of company) {
            drink(guest, entry, guest !== player);
            toast(
                guest,
                guest === player
                    ? `A beer for everyone, ${formatMoney(price)}. The room drinks to you.`
                    : `${player.nickname} buys a round. Cheers!`,
                { title: bar.name, tone: 'gold' },
            );
        }

        cheers(bar);

        return;
    }

    const level = drink(player, entry);

    toast(
        player,
        `${entry.name}, ${formatMoney(price)}. ${entry.strength < 0 ? 'Your head clears a little.' : `${drunk.stageOf(level)}.`}`,
        { title: bar.name, tone: 'good' },
    );
}

// After a blackout: the pavement outside the bar they drank in.
drunk.setWakeProvider((player): Placement | null => {
    const bar =
        barOf(player) ?? current.find((candidate) => candidate.id === lastBar.get(player.id));

    if (!bar) {
        return null;
    }

    interiors.release(player);

    return bar.wake;
});

function nearestDoor(player: Player): { bar: Bar; position: Point; gap: number } | null {
    let best: { bar: Bar; position: Point; gap: number } | null = null;

    for (const bar of current) {
        const interior = maps.current()?.interiors?.find((entry) => entry.id === interiorId(bar));
        const position = bar.door ?? interior?.outside.position;

        if (!position) {
            continue;
        }

        const gap = distance(player.position, position);

        if (!best || gap < best.gap) {
            best = { bar, position, gap };
        }
    }

    return best;
}

commands.register(
    { name: 'bar', usage: '/bar', help: 'Point the compass at the nearest bar.', category: 'City' },
    (player) => {
        const nearest = nearestDoor(player);

        if (!nearest) {
            toast(player, 'There is no bar on this map.', { tone: 'bad' });

            return;
        }

        compass(player, 'bar', COMPASS_PRIORITY, { position: nearest.position });
        pointed.add(player.id);
        toast(
            player,
            `${nearest.bar.name} is ${Math.round(nearest.gap)} m away. At the counter, N picks a drink and B buys it.`,
        );
    },
);

function reset(): void {
    current = barsHere();
    forgetVenues();
    lastBar.clear();
    pointed.clear();
    closed.clear();
}

/**
 * A fire in the bar (firefighters): the staff go at once and come back with
 * the next guest after it reopens.
 */
export function setClosed(barId: Bar['id'], shut: boolean): void {
    if (!shut) {
        closed.delete(barId);

        return;
    }

    closed.add(barId);
    const bar = current.find((candidate) => candidate.id === barId);

    if (bar) {
        standDown(bar);
    }
}

onStart(() => {
    reset();

    if (current.some((bar) => bar.built)) {
        barRoom.build();
    }

    publishDoors();

    for (const player of World.getPlayers()) {
        sendState(player);
    }

    every(TICK_MS, tick);
});

onStop(() => {
    for (const bar of current) {
        standDown(bar);
    }

    barRoom.teardown();
    markersAll('bars', []);
});

// The mission change removed every NPC and map object.
events.on('missionChange', () => {
    barRoom.forget();
    reset();

    if (current.some((bar) => bar.built)) {
        barRoom.build();
    }

    publishDoors();
});

events.on('playerSpawn', (player) => {
    if (life.isTransit(player)) {
        return;
    }

    sendState(player);
    publishDoors(player);
});

events.on('playerRespawn', sendState);
onClient('fr:bar:buy', (player, data: unknown) => buy(player, data));
