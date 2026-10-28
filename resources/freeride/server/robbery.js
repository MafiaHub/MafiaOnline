// Hold-ups. A player with a firearm at a shop counter can /rob it: the
// police are called at once, and the till is theirs if they keep the counter
// for fifteen seconds. Each shop needs ten minutes to restock its till.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {SHOPS} = require("./shops.js");
const maps = require("./lib/maps.js");
const {toast, card, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const COUNTER_REACH = 3.5;
const HOLD_REACH = 7;
const HOLD_MS = 15_000;
const RESTOCK_MS = 10 * 60_000;
const MIN_TAKE = 250;
const MAX_TAKE = 450;
const XP = 60;
const TICK_MS = 500;

const restockedAt = new Map();
const holdups = new Map();
let timer = null;

function armed(player) {
    const weapon = player.getCurrentWeapon();
    return weapon !== 0 && Weapon.get(weapon)?.kind === "firearm";
}

function stop(player, message) {
    const holdup = holdups.get(player.id);
    if (!holdup) return;
    holdups.delete(player.id);
    life.end(player, "robbery");
    card(player, "robbery", null);
    if (message) toast(player, message, {title: holdup.shop.name, tone: "bad"});
}

function tick() {
    const now = Date.now();
    for (const [id, holdup] of holdups) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (!player) {
            holdups.delete(id);
            continue;
        }
        if (!playable(player) || distance(player.position, holdup.shop.position) > HOLD_REACH) {
            stop(player, "You left the counter. The till stays shut.");
            continue;
        }
        const elapsed = now - holdup.startedAt;
        if (elapsed < HOLD_MS) {
            card(player, "robbery", {title: `Hold-up · ${holdup.shop.name}`, subtitle: "Keep the counter. The police are coming.",
                rows: [["Till open in", formatMinutes(HOLD_MS - elapsed)]], progress: elapsed / HOLD_MS, tone: "bad"});
            continue;
        }
        const take = MIN_TAKE + Math.floor(Math.random() * (MAX_TAKE - MIN_TAKE + 1));
        stop(player, null);
        player.giveMoney(take);
        profiles.bump(player, "robberies");
        profiles.addXp(player, XP, "Hold-up");
        Police.setWanted(player, 3);
        toast(player, `${formatMoney(take)} from the till. Now run.`, {title: holdup.shop.name, tone: "gold"});
        feed("police", [name(player, COLORS.red), text(` robbed ${holdup.shop.name} · `), text(formatMoney(take), COLORS.gold)]);
    }
}

commands.register({name: "rob", usage: "/rob", help: "Hold up the shop counter you are standing at.", category: "Combat"}, (player) => {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD || player.getVehicle()) return;
    const shop = maps.current()?.shops && SHOPS.find((entry) => distance(player.position, entry.position) <= COUNTER_REACH);
    if (!shop) {
        toast(player, "Stand at a shop counter first.", {tone: "bad"});
        return;
    }
    if (!armed(player)) {
        toast(player, "Nobody hands over the till to an empty hand. Draw a gun.", {tone: "bad"});
        return;
    }
    const wait = (restockedAt.get(shop.id) ?? 0) - Date.now();
    if (wait > 0) {
        toast(player, `The till is empty. ${shop.name} restocks in ${formatMinutes(wait)}.`, {tone: "bad"});
        return;
    }
    if (!life.begin(player, "robbery", `Robbing ${shop.name}`)) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    restockedAt.set(shop.id, Date.now() + RESTOCK_MS);
    holdups.set(player.id, {shop, startedAt: Date.now()});
    Police.setWanted(player, 2);
    toast(player, "Hands where I can see them! Hold the counter for fifteen seconds.", {title: shop.name, tone: "bad"});
});

menu.provide("robbery", () => SHOPS.map((shop) => ({name: shop.name, readyInSeconds: Math.max(0, Math.ceil(((restockedAt.get(shop.id) ?? 0) - Date.now()) / 1000))})));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => clearInterval(timer));
Events.on("playerDeath", (player) => stop(player, "The hold-up ended with you on the floor."));
Events.on("playerDisconnect", (player) => holdups.delete(player.id));
