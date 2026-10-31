// Client-only shopkeepers and labels. Models never decide a purchase: the
// server checks the player's life, distance, balance, car and inventory.
import { COLORS } from './lib/color';
import { distance, type Point } from './lib/geometry';
import { bindCounterKeys, drawCounterCard } from './lib/panels';
import { livePlayer } from './lib/player';
import { events, log, onServer, onStop } from './lib/runtime';

const INTERACT_DISTANCE = 3.2;
const LABEL_DISTANCE = 24;
const DELIVERY_RETRY_MS = 1_200;
const MODEL_RETRY_MS = 2_000;

interface Offer {
    id: string;
    name: string;
    /** null: priced at the pump; 0: free (accepted, not bought). */
    price: number | null;
    note: string;
}

interface Shop {
    id: string;
    name: string;
    tagline: string;
    position: Point;
    /** Which way the clerk faces, when the shop says. */
    facing?: Point;
    /** The door hinge's frame, and where it is when the frame is not found. */
    entranceFrame?: string;
    entrance?: Point;
    model: string | null;
    animation: string | null;
    offers: Offer[];
}

interface ShopsState {
    generation: number;
    shops: Shop[];
    /** The player carries Salieri's envelope to Pete. */
    courier: boolean;
}

let generation = 0;
let shops: Shop[] = [];
let courier = false;
let lastMoney: number | null = null;
let lastDelivery = 0;
let lastModelAttempt = 0;
// Shop id -> the clerk's Scene handle.
const actors = new Map<string, number>();
// Shop id -> the index of the offer on its card.
const selected = new Map<string, number>();

function nearestShop(player: Player | null): Shop | null {
    if (!player) {
        return null;
    }

    let nearest: { shop: Shop; distance: number } | null = null;

    for (const shop of shops) {
        const gap = distance(player.position, shop.position);

        if (gap <= INTERACT_DISTANCE && (!nearest || gap < nearest.distance)) {
            nearest = { shop, distance: gap };
        }
    }

    return nearest?.shop ?? null;
}

function clearActors(): void {
    for (const handle of actors.values()) {
        Scene.destroy(handle);
    }

    actors.clear();
}

function clearMarket(): void {
    clearActors();
    generation = 0;
    shops = [];
    selected.clear();
    courier = false;
    lastMoney = null;
    Hud.hideScore();
}

function shopFacing(shop: Shop): Point {
    if (shop.facing) {
        return shop.facing;
    }

    const doorHinge = shop.entranceFrame
        ? (World.findWorldFrame(shop.entranceFrame)?.getWorldPosition() ?? shop.entrance)
        : null;

    if (!doorHinge) {
        return { x: 0, y: 0, z: 1 };
    }

    // Pete stands at the doorway. Point out from its hinge toward arrivals,
    // not back into the door leaf.
    const x = shop.position.x - doorHinge.x;
    const z = shop.position.z - doorHinge.z;
    const length = Math.hypot(x, z);

    return length > 0.001 ? { x: x / length, y: 0, z: z / length } : { x: 0, y: 0, z: 1 };
}

function placeActors(): void {
    if (!World.isReady() || generation !== World.getMissionGeneration()) {
        return;
    }

    for (const shop of shops) {
        if (!shop.model || actors.has(shop.id) || !Scene.canUseHumanModel(shop.model)) {
            continue;
        }

        const handle = Scene.createHuman(shop.model, shop.position, shopFacing(shop), true);

        if (handle === null) {
            continue;
        }

        actors.set(shop.id, handle);

        if (shop.animation && !Scene.playHumanAnimation(handle, shop.animation, true)) {
            log.warn(`Could not play ${shop.animation} for ${shop.name}`);
        }
    }
}

function selectedOffer(shop: Shop): Offer {
    return shop.offers[(selected.get(shop.id) ?? 0) % shop.offers.length];
}

function buyNearest(): void {
    const shop = nearestShop(livePlayer());

    if (!shop) {
        return;
    }

    events.emitServer('fr:shop:buy', {
        generation,
        shopId: shop.id,
        offerId: selectedOffer(shop).id,
    });
}

function nextOffer(): void {
    const shop = nearestShop(livePlayer());

    if (!shop || shop.offers.length < 2) {
        return;
    }

    selected.set(shop.id, ((selected.get(shop.id) ?? 0) + 1) % shop.offers.length);
    Sound.play('00_kompas.wav', { volume: 0.45 });
}

function priceOf(offer: Offer): string {
    if (offer.price === null) {
        return 'PRICE AT PUMP';
    }

    return offer.price === 0 ? 'NO CHARGE' : `$${offer.price}`;
}

function drawShopCard(shop: Shop, player: Player): void {
    const offer = selectedOffer(shop);

    drawCounterCard({
        title: shop.name,
        tagline: shop.tagline,
        offer: `${offer.name}  ·  ${priceOf(offer)}`,
        note: offer.note,
        index: selected.get(shop.id) ?? 0,
        count: shop.offers.length,
        action: offer.price === 0 ? 'ACCEPT' : 'BUY',
        money: player.money,
    });
}

// The watch and the money counter share the top-right corner; the watch has
// it while a countdown runs (a jail sentence).
function showMoney(player: Player): void {
    if (Hud.getCountdown() !== null) {
        if (Hud.isScoreVisible()) {
            Hud.hideScore();
        }

        lastMoney = null;
    } else if (
        (lastMoney !== player.money ||
            Hud.getScore() !== player.money ||
            Hud.isScoreVisible() !== true) &&
        Hud.setScore(player.money)
    ) {
        lastMoney = player.money;
    }
}

bindCounterKeys(buyNearest, nextOffer);

onServer<ShopsState>('fr:shops:state', (state) => {
    if (state.generation !== World.getMissionGeneration() || !Array.isArray(state.shops)) {
        return;
    }

    if (generation !== state.generation) {
        clearActors();
    }

    generation = state.generation;
    shops = state.shops;
    courier = Boolean(state.courier);
    placeActors();
});

events.on('missionUnload', clearMarket);
onStop(clearMarket);

events.on('render', () => {
    const player = livePlayer();

    if (!player || generation !== World.getMissionGeneration()) {
        return;
    }

    showMoney(player);

    const now = Date.now();

    if (
        actors.size < shops.filter((shop) => shop.model).length &&
        now - lastModelAttempt > MODEL_RETRY_MS
    ) {
        lastModelAttempt = now;
        placeActors();
    }

    for (const shop of shops) {
        if (distance(player.position, shop.position) > LABEL_DISTANCE) {
            continue;
        }

        const top = { x: shop.position.x, y: shop.position.y + 2.15, z: shop.position.z };

        Draw.worldText(shop.name.toUpperCase(), top, 16, COLORS.gold, 3, true);
    }

    const shop = nearestShop(player);

    if (!shop) {
        return;
    }

    drawShopCard(shop, player);

    if (shop.id === 'pete' && courier && now - lastDelivery > DELIVERY_RETRY_MS) {
        lastDelivery = now;
        events.emitServer('fr:shop:deliver', {});
    }
});
