// Street combat: the kill feed, kill streaks and bounties. A streak of three
// puts a price on a player's head that grows with every kill; the police add
// one for anyone at the lethal wanted level. Whoever drops the target takes
// the money, and a target who lasts ten minutes keeps half of it.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const nametag = require("./lib/nametag.js");
const {toast, toastAll, feed, name, text, compass, COLORS} = require("./lib/notify.js");
const {formatMoney, findPlayer, onStart, onStop, playerById} = require("./lib/util.js");

const KILL_XP = 50;
const STREAK_FOR_BOUNTY = 3;
const FIRST_BOUNTY = 250;
const BOUNTY_STEP = 150;
const MAX_BOUNTY = 2000;
const POLICE_BOUNTY = 300;
const BOUNTY_SURVIVE_MS = 10 * 60_000;

const CAUSES = {
    script: "died", fall: "fell to their death", drowning: "drowned", fire: "burned",
    explosion: "was caught in a blast", vehicle: "was run down", railway: "was hit by a tram",
    firearm: "was shot", melee: "was beaten",
};

const streaks = new Map();
const bounties = new Map();
let timer = null;

function sameCrew(a, b) {
    const crewA = a.state.get("fr.crew");
    return Boolean(crewA && crewA.tag === b.state.get("fr.crew")?.tag);
}

function weaponName(info) {
    if (info.weaponId === null || info.weaponId === undefined) return null;
    if (info.weaponId === 0) return "fists";
    return Weapon.get(info.weaponId)?.name ?? null;
}

function setBounty(player, amount, reason) {
    const current = bounties.get(player.id);
    const value = Math.min(MAX_BOUNTY, amount);
    bounties.set(player.id, {amount: value, reason, since: current?.since ?? Date.now()});
    profiles.setPublic(player, "fr.bounty", value);
    nametag.refresh(player);
}

function clearBounty(player) {
    if (!bounties.delete(player.id)) return;
    profiles.setPublic(player, "fr.bounty", null);
    nametag.refresh(player);
    for (const hunter of World.getPlayers()) compass(hunter, `hunt:${player.id}`, 30, null);
}

function claim(victim, killer) {
    const bounty = bounties.get(victim.id);
    if (!bounty) return;
    clearBounty(victim);
    if (!killer) return;
    killer.giveMoney(bounty.amount);
    profiles.bump(killer, "bountiesClaimed");
    profiles.addXp(killer, Math.round(bounty.amount / 4), "Bounty claimed");
    feed("bounty", [name(killer, COLORS.gold), text(" collected the "), text(formatMoney(bounty.amount), COLORS.gold),
        text(" bounty on "), name(victim)]);
}

function onKill(killer, victim) {
    profiles.bump(killer, "kills");
    profiles.addXp(killer, KILL_XP, `Took down ${victim.nickname}`);
    const streak = (streaks.get(killer.id) ?? 0) + 1;
    streaks.set(killer.id, streak);
    const record = profiles.get(killer);
    if (record && streak > record.bestStreak) profiles.setField(killer, "bestStreak", streak);
    profiles.publish(killer);
    // An officer on duty is doing the job, not running up a streak.
    if (streak < STREAK_FOR_BOUNTY || killer.state.get("fr.duty")) return;
    const amount = streak === STREAK_FOR_BOUNTY ? FIRST_BOUNTY : (bounties.get(killer.id)?.amount ?? FIRST_BOUNTY) + BOUNTY_STEP;
    setBounty(killer, amount, "streak");
    feed("bounty", [name(killer, COLORS.red), text(` is on a ${streak}-kill streak · `), text(`${formatMoney(Math.min(MAX_BOUNTY, amount))} bounty`, COLORS.gold)]);
    if (streak === STREAK_FOR_BOUNTY) {
        toast(killer, "Three in a row. There is a price on your head now; stay alive ten minutes to keep half.", {title: "Wanted man", tone: "bad"});
    }
}

Events.on("playerDeath", (victim, killer, info) => {
    if (life.activity(victim)?.kind === "duel") return; // duels.js keeps its own score.
    profiles.bump(victim, "deaths");
    streaks.delete(victim.id);
    const weapon = weaponName(info);
    // The killer can also be a simulation host Npc: a policeman or a gangster.
    const valid = killer instanceof Player && killer.id !== victim.id;
    if (killer && !valid && killer.id !== victim.id) {
        feed("death", [name(victim), text(killer.kind === "police" ? " was gunned down by the police" : " was killed by a gangster")]);
        claim(victim, null);
    } else if (valid) {
        const friendly = sameCrew(killer, victim);
        feed("kill", [name(killer, friendly ? COLORS.muted : COLORS.white), text(weapon ? `  ${weapon}  ` : "  killed  ", COLORS.red),
            name(victim, COLORS.white), ...(friendly ? [text("  (crewmate)")] : [])]);
        claim(victim, friendly ? null : killer);
        if (!friendly) onKill(killer, victim);
    } else {
        feed("death", [name(victim), text(` ${CAUSES[info.cause] ?? "died"}`)]);
        claim(victim, null);
    }
    profiles.publish(victim);
});

Events.on("playerWantedChange", (player, {level}) => {
    const bounty = bounties.get(player.id);
    if (level >= 3 && !bounty) {
        setBounty(player, POLICE_BOUNTY, "police");
        toastAll(`The police put ${formatMoney(POLICE_BOUNTY)} on ${player.nickname}, dead or alive.`, {title: "Most wanted", tone: "bad"});
    } else if (level === 0 && bounty?.reason === "police") {
        clearBounty(player);
    }
});

function tickBounties() {
    const now = Date.now();
    for (const [id, bounty] of bounties) {
        const player = playerById(id);
        if (!player) {
            bounties.delete(id);
            continue;
        }
        if (bounty.reason === "streak" && now - bounty.since >= BOUNTY_SURVIVE_MS) {
            const kept = Math.round(bounty.amount / 2);
            clearBounty(player);
            player.giveMoney(kept);
            profiles.addXp(player, 75, "Survived the bounty");
            feed("bounty", [name(player, COLORS.gold), text(" outlived their bounty and kept "), text(formatMoney(kept), COLORS.gold)]);
        }
    }
}

commands.register({name: "hunt", aliases: ["bounty", "bounties"], usage: "/hunt [player|off]", help: "Point your compass at a bounty target.", category: "Combat"}, (player, args) => {
    const query = args.join(" ").trim();
    if (query.toLowerCase() === "off") {
        for (const id of bounties.keys()) compass(player, `hunt:${id}`, 30, null);
        return;
    }
    const target = query ? findPlayer(query)
        : [...bounties.entries()].filter(([id]) => id !== player.id).sort((a, b) => b[1].amount - a[1].amount).map(([id]) => playerById(id))[0];
    if (!target || !bounties.has(target.id) || target.id === player.id) {
        toast(player, bounties.size ? "No bounty on that name." : "Nobody has a price on their head right now.");
        return;
    }
    for (const id of bounties.keys()) compass(player, `hunt:${id}`, 30, null);
    compass(player, `hunt:${target.id}`, 30, {playerId: target.id});
    toast(player, `Hunting ${target.nickname}: ${formatMoney(bounties.get(target.id).amount)}. /hunt off to stop.`, {tone: "gold"});
});

menu.provide("bounties", () => [...bounties.entries()].map(([id, bounty]) => ({
    id, name: playerById(id)?.nickname ?? "?", amount: bounty.amount, reason: bounty.reason,
})).sort((a, b) => b.amount - a.amount));

onStart(() => { timer = setInterval(tickBounties, 5000); });
onStop(() => clearInterval(timer));
Events.on("playerDisconnect", (player) => {
    streaks.delete(player.id);
    if (bounties.delete(player.id)) for (const hunter of World.getPlayers()) compass(hunter, `hunt:${player.id}`, 30, null);
});
Events.on("playerSpawn", (player) => nametag.refresh(player));
