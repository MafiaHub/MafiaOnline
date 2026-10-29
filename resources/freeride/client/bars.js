// The drink menu at a bar counter (server/bars.js). The server checks the
// player's bar, distance, money and the barman; this only shows the card and
// sends the choice. N picks the next drink, B orders it.
const BAR_LABEL_DISTANCE = 18;
const BAR_LABEL_COLOR = 0xffe7c47d;
const BAR_TEXT_COLOR = 0xfff5e6cb;
const BAR_PANEL_FILL = 0xe6121519;
const BAR_PANEL_LINE = 0xff9c7743;
const BAR_PANEL_MUTED = 0xffb6aa91;

// Within this of a shop counter (client/shops.js) its card has the keys.
const SHOP_REACH = 3.2;

const tavern = {generation: 0, bars: [], selected: new Map(), shops: []};

function barDistance(position, bar) {
    const a = position;
    const b = bar.counter.position;
    return {flat: Math.hypot(a.x - b.x, a.z - b.z), height: Math.abs(a.y - b.y)};
}

function counterHere() {
    const player = LocalPlayer;
    if (!player?.spawned || !player.alive || player.getVehicle() || tavern.generation !== World.getMissionGeneration()) return null;
    const position = player.getWorldPosition() ?? player.position;
    if (tavern.shops.some((shop) => Math.hypot(position.x - shop.x, position.y - shop.y, position.z - shop.z) <= SHOP_REACH)) return null;
    return tavern.bars.find((bar) => {
        const gap = barDistance(position, bar);
        return player.virtualWorld === bar.world && gap.flat <= bar.counter.reach && gap.height <= 1.5;
    }) ?? null;
}

function selectedDrink(bar) {
    return bar.drinks[(tavern.selected.get(bar.id) ?? 0) % bar.drinks.length];
}

function orderDrink() {
    const bar = counterHere();
    if (!bar) return;
    Events.emitServer("fr:bar:buy", {generation: tavern.generation, barId: bar.id, drinkId: selectedDrink(bar).id});
}

function nextDrink() {
    const bar = counterHere();
    if (!bar) return;
    tavern.selected.set(bar.id, ((tavern.selected.get(bar.id) ?? 0) + 1) % bar.drinks.length);
    Sound.play("00_kompas.wav", {volume: 0.45});
}

function drawCard(bar, player) {
    const drink = selectedDrink(bar);
    const index = (tavern.selected.get(bar.id) ?? 0) % bar.drinks.length;
    const price = drink.price === null ? "A ROUND" : `$${drink.price}`;
    const {width, height} = Web.getScreenSize();
    const cardWidth = Math.min(520, width - 32);
    const cardHeight = 184;
    const left = (width - cardWidth) / 2;
    const top = height - cardHeight - 28;
    const center = width / 2;
    Draw.rect(left, top, cardWidth, cardHeight, BAR_PANEL_FILL);
    Draw.line(left + 12, top + 11, left + cardWidth - 12, top + 11, 2, BAR_PANEL_LINE);
    Draw.text(bar.name.toUpperCase(), center, top + 38, 22, BAR_TEXT_COLOR, 3);
    Draw.text(bar.tagline.toUpperCase(), center, top + 61, 12, BAR_PANEL_MUTED, 3);
    Draw.line(left + 28, top + 70, left + cardWidth - 28, top + 70, 1, BAR_PANEL_LINE);
    Draw.text(`${drink.name}  ·  ${price}`, center, top + 102, 19, BAR_TEXT_COLOR, 3);
    Draw.text(drink.note, center, top + 126, 12, BAR_PANEL_MUTED, 3);
    Draw.line(left + 28, top + 138, left + cardWidth - 28, top + 138, 1, BAR_PANEL_LINE);
    const next = Settings.getKeyLabel("n").toUpperCase();
    const order = Settings.getKeyLabel("b").toUpperCase();
    Draw.text(`[${next}] NEXT  ${index + 1}/${bar.drinks.length}      [${order}] ORDER      CASH $${player.money}`, center, top + 164, 13, BAR_LABEL_COLOR, 3);
}

Settings.describeKey("b", "Buy or accept at a shop or bar counter");
Settings.describeKey("n", "Next offer at a shop or bar counter");
Key.bind("b", orderDrink);
Key.bind("n", nextDrink);

Events.on("fr:bars:state", (state) => {
    if (!state || state.generation !== World.getMissionGeneration() || !Array.isArray(state.bars)) return;
    tavern.generation = state.generation;
    tavern.bars = state.bars;
});

Events.on("fr:shops:state", (state) => {
    if (state && Array.isArray(state.shops)) tavern.shops = state.shops.map((shop) => shop.position).filter(Boolean);
});

Events.on("missionUnload", () => {
    tavern.shops = [];
    tavern.generation = 0;
    tavern.bars = [];
    tavern.selected.clear();
});

Events.on("render", () => {
    const player = LocalPlayer;
    if (!player?.spawned || !player.alive || tavern.generation !== World.getMissionGeneration()) return;
    const position = player.getWorldPosition() ?? player.position;
    for (const bar of tavern.bars) {
        if (player.virtualWorld !== bar.world || barDistance(position, bar).flat > BAR_LABEL_DISTANCE) continue;
        const {x, y, z} = bar.counter.position;
        Draw.worldText("BAR · N / B", {x, y: y + 1.9, z}, 15, BAR_LABEL_COLOR, 3, true);
    }
    const bar = counterHere();
    if (bar) drawCard(bar, player);
});
