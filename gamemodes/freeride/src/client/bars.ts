// The drink menu at a bar counter (server/bars/). The server checks the
// player's bar, distance, money and the barman; this only shows the card and
// sends the choice. N picks the next drink, B orders it.
import { COLORS } from './lib/color';
import { distance, flatDistance, type Point } from './lib/geometry';
import { bindCounterKeys, drawCounterCard } from './lib/panels';
import { livePlayer } from './lib/player';
import { events, onServer } from './lib/runtime';

const LABEL_DISTANCE = 18;

// Within this of a shop counter (client/shops.ts) its card has the keys.
const SHOP_REACH = 3.2;

interface Drink {
    id: string;
    name: string;
    /** null: a round for the house, priced per head. */
    price: number | null;
    note: string;
}

interface Bar {
    id: string;
    name: string;
    tagline: string;
    world: number;
    counter: { position: Point; reach: number };
    drinks: Drink[];
}

interface BarsState {
    generation: number;
    bars: Bar[];
}

interface ShopsState {
    shops: { position: Point }[];
}

let generation = 0;
let bars: Bar[] = [];
let shopCounters: Point[] = [];
// Bar id -> the index of the drink on its card.
const selected = new Map<string, number>();

function counterHere(): Bar | null {
    const player = livePlayer();

    if (!player || player.getVehicle() || generation !== World.getMissionGeneration()) {
        return null;
    }

    const position = player.getWorldPosition() ?? player.position;

    if (shopCounters.some((shop) => distance(position, shop) <= SHOP_REACH)) {
        return null;
    }

    return (
        bars.find(
            (bar) =>
                player.virtualWorld === bar.world &&
                flatDistance(position, bar.counter.position) <= bar.counter.reach &&
                Math.abs(position.y - bar.counter.position.y) <= 1.5,
        ) ?? null
    );
}

function selectedIndex(bar: Bar): number {
    return (selected.get(bar.id) ?? 0) % bar.drinks.length;
}

function orderDrink(): void {
    const bar = counterHere();

    if (!bar) {
        return;
    }

    events.emitServer('fr:bar:buy', {
        generation,
        barId: bar.id,
        drinkId: bar.drinks[selectedIndex(bar)].id,
    });
}

function nextDrink(): void {
    const bar = counterHere();

    if (!bar) {
        return;
    }

    selected.set(bar.id, ((selected.get(bar.id) ?? 0) + 1) % bar.drinks.length);
    Sound.play('00_kompas.wav', { volume: 0.45 });
}

function drawCard(bar: Bar, player: Player): void {
    const index = selectedIndex(bar);
    const drink = bar.drinks[index];
    const price = drink.price === null ? 'A ROUND' : `$${drink.price}`;

    drawCounterCard({
        title: bar.name,
        tagline: bar.tagline,
        offer: `${drink.name}  ·  ${price}`,
        note: drink.note,
        index,
        count: bar.drinks.length,
        action: 'ORDER',
        money: player.money,
    });
}

bindCounterKeys(orderDrink, nextDrink);

onServer<BarsState>('fr:bars:state', (state) => {
    if (state.generation !== World.getMissionGeneration() || !Array.isArray(state.bars)) {
        return;
    }

    generation = state.generation;
    bars = state.bars;
});

onServer<ShopsState>('fr:shops:state', (state) => {
    if (Array.isArray(state.shops)) {
        shopCounters = state.shops.map((shop) => shop.position).filter(Boolean);
    }
});

events.on('missionUnload', () => {
    shopCounters = [];
    generation = 0;
    bars = [];
    selected.clear();
});

events.on('render', () => {
    const player = livePlayer();

    if (!player || generation !== World.getMissionGeneration()) {
        return;
    }

    const position = player.getWorldPosition() ?? player.position;

    for (const bar of bars) {
        if (
            player.virtualWorld !== bar.world ||
            flatDistance(position, bar.counter.position) > LABEL_DISTANCE
        ) {
            continue;
        }

        const { x, y, z } = bar.counter.position;

        Draw.worldText('BAR · N / B', { x, y: y + 1.9, z }, 15, COLORS.gold, 3, true);
    }

    const bar = counterHere();

    if (bar) {
        drawCard(bar, player);
    }
});
