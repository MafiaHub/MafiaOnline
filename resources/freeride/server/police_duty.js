// Police duty: a player can put on the uniform, take a squad car and hunt the
// players the city's police want or who carry a bounty. Taking down a suspect
// pays by wanted level; shooting a citizen costs the badge and a fine.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const nametag = require("./lib/nametag.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, compass, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, headingOf, vehicleById, formatMoney, onStart, onStop} = require("./lib/util.js");

const UNIFORM = "Pol01.i3d";
const SQUAD_CAR = "poli00.i3d";
const KIT = Object.freeze({weapons: [{weaponId: 6, loaded: 6, reserve: 36}, {weaponId: 11, loaded: 8, reserve: 24}], selected: 6});
const PAY_PER_LEVEL = 150;
const CITIZEN_FINE = 250;
const XP = 60;
const TICK_MS = 2000;

const officers = new Map();
let timer = null;

function suspect(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD || officers.has(player.id)) return null;
    const level = Police.getRecord(player)?.level ?? 0;
    const bounty = player.state.get("fr.bounty") ?? 0;
    return level > 0 || bounty > 0 ? {player, level, bounty} : null;
}

function endDuty(player, message, tone = "info") {
    const duty = officers.get(player.id);
    if (!duty) return;
    officers.delete(player.id);
    life.clearOverride(player);
    life.end(player, "duty");
    profiles.setPublic(player, "fr.duty", null);
    nametag.refresh(player);
    compass(player, "duty", 45, null);
    card(player, "duty", null);
    if (World.getPlayers().some((other) => other.id === player.id)) {
        player.setModel(duty.model);
        if (player.alive) player.setInventory(duty.loadout.weapons, duty.loadout.selected || undefined);
    }
    const car = vehicleById(duty.carId);
    if (car && car.getOccupants().every((occupant) => occupant === null || occupant.id === player.id)) setTimeout(() => {
        const current = vehicleById(duty.carId);
        if (current && current.getOccupants().every((occupant) => occupant === null)) current.destroy();
    }, 3000);
    if (message) toast(player, message, {title: "Police duty", tone});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    if ((Police.getRecord(player)?.level ?? 0) > 0 || player.state.get("fr.bounty")) {
        toast(player, "The precinct will not swear in a wanted man.", {tone: "bad"});
        return;
    }
    if (!life.begin(player, "duty", "Police duty")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    const duty = {model: player.model, loadout: life.loadoutOf(player), carId: null, pay: 0};
    officers.set(player.id, duty);
    player.setModel(UNIFORM);
    life.giveKit(player, KIT);
    life.setOverride(player, {kit: KIT});
    profiles.setPublic(player, "fr.duty", true);
    nametag.refresh(player);
    if (player.getVehicle()) player.removeFromVehicle();
    const heading = headingOf(player.rotation);
    const {x, y, z} = player.position;
    const car = Vehicle.spawn(SQUAD_CAR, {x: x + Math.cos(heading) * 3.5, y: y + 0.3, z: z - Math.sin(heading) * 3.5}, heading, player);
    if (car) {
        duty.carId = car.id;
        car.state.set("fr.fullTank", true);
        car.state.set("fr.owner", player.nickname);
        car.setRadarMarker(true, 0x6fa8ff);
        player.putInVehicle(car, 0);
    }
    feed("police", [name(player, "#8fb4d8"), text(" went on police duty")]);
    toast(player, "On duty. Wanted players and bounty targets show on your compass. Leave citizens alone. /duty to clock off.",
        {title: "Police duty", tone: "gold", seconds: 9});
}

function tick() {
    const suspects = World.getPlayers().map(suspect).filter(Boolean);
    for (const [id, duty] of officers) {
        const officer = World.getPlayers().find((player) => player.id === id);
        if (!officer) {
            officers.delete(id);
            continue;
        }
        if (!officer.spawned) continue;
        const ranked = suspects.map((entry) => ({...entry, distance: distance(officer.position, entry.player.position)}))
            .sort((a, b) => a.distance - b.distance);
        compass(officer, "duty", 45, ranked[0] ? {playerId: ranked[0].player.id} : null);
        card(officer, "duty", {title: "Police duty", subtitle: ranked.length ? "Nearest suspect on your compass" : "Quiet streets. Patrol.",
            rows: ranked.slice(0, 3).map((entry) => [entry.player.nickname,
                `${entry.level ? `wanted ${entry.level}` : formatMoney(entry.bounty)} · ${Math.round(entry.distance)} m`])
                .concat([["Earned this shift", formatMoney(duty.pay)]]), tone: "info"});
    }
}

Events.on("playerDeath", (victim, killer) => {
    if (!(killer instanceof Player) || killer.id === victim.id || !officers.has(killer.id)) return;
    const level = Police.getRecord(victim)?.level ?? 0;
    const bounty = victim.state.get("fr.bounty") ?? 0;
    if (level === 0 && !bounty) {
        const fine = Math.min(killer.money, CITIZEN_FINE);
        killer.giveMoney(-fine);
        endDuty(killer, `You shot a citizen. Badge and ${formatMoney(fine)} taken.`, "bad");
        feed("police", [name(killer, COLORS.red), text(" lost their badge")]);
        return;
    }
    const pay = PAY_PER_LEVEL * Math.max(1, level);
    killer.giveMoney(pay);
    officers.get(killer.id).pay += pay;
    profiles.bump(killer, "arrests");
    profiles.addXp(killer, XP, "Took down a suspect");
    Police.clear(victim);
    feed("police", [text("Officer ", "#8fb4d8"), name(killer, "#8fb4d8"), text(" took down "), name(victim)]);
});

commands.register({name: "duty", aliases: ["cop", "police"], usage: "/duty", help: "Go on or off police duty.", category: "City"}, (player) => {
    if (officers.has(player.id)) endDuty(player, "Off duty. Uniform and squad car returned.");
    else startDuty(player);
});

menu.provide("duty", (player) => ({onDuty: officers.has(player.id), officers: officers.size,
    suspects: World.getPlayers().map(suspect).filter(Boolean).length}));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => {
    clearInterval(timer);
    for (const id of [...officers.keys()]) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (player) endDuty(player, null);
    }
});
Events.on("playerDisconnect", (player) => endDuty(player, null));
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) endDuty(player, "Your shift ended with the city reloading.");
});

module.exports = {onDuty: (player) => officers.has(player.id)};
