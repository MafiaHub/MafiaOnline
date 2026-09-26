// Client-only shopkeepers and labels. Models never decide a purchase: the
// server checks the player's life, distance, balance, car and inventory.
const SHOP_INTERACT_DISTANCE = 3.2;
const SHOP_LABEL_DISTANCE = 24;
const SHOP_DELIVERY_RETRY_MS = 1_200;
const SHOP_LABEL_COLOR = 0xffe7c47d;
const SHOP_PAGE_TEXT_COLOR = 0xfff5e6cb;
const SHOP_PANEL_FILL = 0xe6121519;
const SHOP_PANEL_LINE = 0xff9c7743;
const SHOP_PANEL_MUTED = 0xffb6aa91;

const market = {generation: 0, shops: [], actors: new Map(), selected: new Map(), courier: false,
    lastMoney: null, lastDelivery: 0, lastModelAttempt: 0};

function shopDistance(player, shop) {
    const a = player.position;
    const b = shop.position;
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function nearestShop(player) {
    if (!player?.spawned || !player.alive) return null;
    let nearest = null;
    for (const shop of market.shops) {
        const distance = shopDistance(player, shop);
        if (distance <= SHOP_INTERACT_DISTANCE && (!nearest || distance < nearest.distance)) nearest = {shop, distance};
    }
    return nearest?.shop ?? null;
}

function clearActors() {
    for (const handle of market.actors.values()) Scene.destroy(handle);
    market.actors.clear();
}

function clearMarket() {
    clearActors();
    market.generation = 0;
    market.shops = [];
    market.selected.clear();
    market.courier = false;
    market.lastMoney = null;
    Hud.hideScore();
}

function shopFacing(shop) {
    const doorHinge = shop.entranceFrame
        ? World.findWorldFrame(shop.entranceFrame)?.getWorldPosition() ?? shop.entrance
        : null;
    if (!doorHinge) return {x: 0, y: 0, z: 1};
    // Pete stands at the doorway. Point out from its hinge toward arrivals,
    // not back into the door leaf.
    const x = shop.position.x - doorHinge.x;
    const z = shop.position.z - doorHinge.z;
    const length = Math.hypot(x, z);
    return length > 0.001 ? {x: x / length, y: 0, z: z / length} : {x: 0, y: 0, z: 1};
}

function placeActors() {
    if (!World.isReady() || market.generation !== World.getMissionGeneration()) return;
    for (const shop of market.shops) {
        if (market.actors.has(shop.id) || !Scene.canUseHumanModel(shop.model)) continue;
        const handle = Scene.createHuman(shop.model, shop.position, shopFacing(shop), true);
        if (handle === null) continue;
        market.actors.set(shop.id, handle);
        if (shop.animation && !Scene.playHumanAnimation(handle, shop.animation, true)) {
            console.warn(`[mafia1online-sample] Could not play ${shop.animation} for ${shop.name}`);
        }
    }
}

function selectedOffer(shop) {
    const index = market.selected.get(shop.id) ?? 0;
    return shop.offers[index % shop.offers.length];
}

function buyNearest() {
    const shop = nearestShop(LocalPlayer);
    if (!shop) return;
    const offer = selectedOffer(shop);
    Events.emitServer("sample:shop:buy", {
        generation: market.generation, shopId: shop.id, offerId: offer.id,
    });
}

function nextOffer() {
    const shop = nearestShop(LocalPlayer);
    if (!shop || shop.offers.length < 2) return;
    market.selected.set(shop.id, ((market.selected.get(shop.id) ?? 0) + 1) % shop.offers.length);
    Sound.play("00_kompas.wav", {volume: 0.45});
}

function drawShopCard(shop, player) {
    const offer = selectedOffer(shop);
    const index = market.selected.get(shop.id) ?? 0;
    const price = offer.price === null ? "PRICE AT PUMP" : offer.price === 0 ? "NO CHARGE" : `$${offer.price}`;
    const {width, height} = Web.getScreenSize();
    const cardWidth = Math.min(520, width - 32);
    const cardHeight = 184;
    const left = (width - cardWidth) / 2;
    const top = height - cardHeight - 28;
    const center = width / 2;
    Draw.rect(left, top, cardWidth, cardHeight, SHOP_PANEL_FILL);
    Draw.line(left + 12, top + 11, left + cardWidth - 12, top + 11, 2, SHOP_PANEL_LINE);
    Draw.text(shop.name.toUpperCase(), center, top + 38, 22, SHOP_PAGE_TEXT_COLOR, 3);
    Draw.text(shop.tagline.toUpperCase(), center, top + 61, 12, SHOP_PANEL_MUTED, 3);
    Draw.line(left + 28, top + 70, left + cardWidth - 28, top + 70, 1, SHOP_PANEL_LINE);
    Draw.text(`${offer.name}  ·  ${price}`, center, top + 102, 19, SHOP_PAGE_TEXT_COLOR, 3);
    Draw.text(offer.note, center, top + 126, 12, SHOP_PANEL_MUTED, 3);
    Draw.line(left + 28, top + 138, left + cardWidth - 28, top + 138, 1, SHOP_PANEL_LINE);
    Draw.text(`[N] NEXT  ${index + 1}/${shop.offers.length}      [B] ${offer.price === 0 ? "ACCEPT" : "BUY"}      CASH $${player.money}`,
        center, top + 164, 13, SHOP_LABEL_COLOR, 3);
}

Key.bind("b", buyNearest);
Key.bind("n", nextOffer);

Events.on("sample:shops:state", (state) => {
    if (!state || state.generation !== World.getMissionGeneration() || !Array.isArray(state.shops)) return;
    const changed = market.generation !== state.generation;
    if (changed) clearActors();
    market.generation = state.generation;
    market.shops = state.shops;
    market.courier = Boolean(state.courier);
    placeActors();
});

Events.on("missionUnload", clearMarket);
Events.on("resourceStop", (name) => { if (name === "mafia1online-sample") clearMarket(); });

Events.on("render", () => {
    const player = LocalPlayer;
    if (!player?.spawned || !player.alive || market.generation !== World.getMissionGeneration()) return;
    if ((market.lastMoney !== player.money || Hud.getScore() !== player.money || Hud.isScoreVisible() !== true)
        && Hud.setScore(player.money)) market.lastMoney = player.money;
    const now = Date.now();
    if (market.actors.size < market.shops.length && now - market.lastModelAttempt > 2_000) {
        market.lastModelAttempt = now;
        placeActors();
    }
    for (const shop of market.shops) {
        if (shopDistance(player, shop) > SHOP_LABEL_DISTANCE) continue;
        const top = {x: shop.position.x, y: shop.position.y + 2.15, z: shop.position.z};
        Draw.worldText(shop.name.toUpperCase(), top, 16, SHOP_LABEL_COLOR, 3, true);
    }
    const shop = nearestShop(player);
    if (!shop) return;
    drawShopCard(shop, player);
    if (shop.id === "pete" && market.courier && now - market.lastDelivery > SHOP_DELIVERY_RETRY_MS) {
        market.lastDelivery = now;
        Events.emitServer("sample:shop:deliver", {});
    }
});
