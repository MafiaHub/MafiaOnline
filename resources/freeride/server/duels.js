// Duels in a private copy of the city. Both players move to their own virtual
// world where nobody else can see or interrupt them, fight rounds from where
// they stood, and return with the weapons and health they had before.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const invites = require("./lib/invites.js");
const commands = require("./lib/commands.js");
const {toast, card, feed, name, text, announce, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, findPlayer, formatMoney, headingOf, plain, onStart, onStop} = require("./lib/util.js");

const FIRST_DUEL_WORLD = 0x20000000;
const MAX_REACH = 80;
const ROUND_RESET_MS = 3000;
const DUEL_LIMIT_MS = 8 * 60_000;
const MAX_STAKE = 5000;
const DUEL_XP = 80;

const KITS = {
    colt: {label: "Colt .45", weapons: [{weaponId: 6, loaded: 6, reserve: 30}], selected: 6},
    shotgun: {label: "Shotgun", weapons: [{weaponId: 11, loaded: 8, reserve: 32}], selected: 11},
    thompson: {label: "Thompson", weapons: [{weaponId: 10, loaded: 50, reserve: 100}], selected: 10},
    bat: {label: "Baseball bat", weapons: [{weaponId: 4}], selected: 4},
};

const duels = new Map();
let nextDuelId = 1;

function duelOf(player) {
    for (const duel of duels.values()) if (duel.sides.some((side) => side.player.id === player.id)) return duel;
    return null;
}

// Why a player cannot duel right now, phrased to follow "is" or "are".
function eligible(player, other) {
    if (!playable(player)) return "not on the street";
    if (player.virtualWorld !== MAIN_WORLD) return "somewhere else";
    if (player.getVehicle()) return "in a car";
    if (life.activity(player)) return "busy";
    if ((Police.getRecord(player)?.level ?? 0) > 0) return "wanted by the police";
    if (other && distance(player.position, other.position) > MAX_REACH) return `more than ${MAX_REACH} m away`;
    return null;
}

function explain(player, about, problem) {
    return player.id === about.id ? `You are ${problem}.` : `${about.nickname} is ${problem}.`;
}

function scoreRows(duel) {
    return duel.sides.map((side) => [side.player.nickname, `${side.score} / ${duel.firstTo}`]);
}

function showCards(duel) {
    for (const side of duel.sides) {
        card(side.player, "duel", {title: `Duel · ${KITS[duel.kit].label}`, subtitle: `First to ${duel.firstTo}${duel.stake ? ` · ${formatMoney(duel.stake * 2)} pot` : ""}`,
            rows: scoreRows(duel), tone: "bad"});
    }
}

// Spawning fails while any client is still loading; keep trying while the
// move is still wanted.
function placeUntil(player, spot, loadout, wanted) {
    if (!wanted() || !World.getPlayers().some((other) => other.id === player.id)) return;
    if (!life.teleport(player, spot.position, spot.heading, loadout)) setTimeout(() => placeUntil(player, spot, loadout, wanted), 1000);
}

function resetRound(duel) {
    if (!duels.has(duel.id)) return;
    duel.round++;
    const round = duel.round;
    for (const side of duel.sides) {
        placeUntil(side.player, side.spot, {...KITS[duel.kit], health: 100}, () => duels.has(duel.id) && duel.round === round);
        announce(side.player, `ROUND ${duel.round}`, 2, "beep50.wav");
    }
    showCards(duel);
}

function begin(challenger, target, firstTo, stake, kit) {
    for (const [player, other] of [[challenger, target], [target, challenger]]) {
        const problem = eligible(player, other);
        if (problem) {
            toast(challenger, explain(challenger, player, problem), {tone: "bad"});
            toast(target, explain(target, player, problem), {tone: "bad"});
            return;
        }
    }
    if (stake) {
        if (!challenger.trySpendMoney(stake)) {
            toast(challenger, `You cannot cover the ${formatMoney(stake)} stake.`, {tone: "bad"});
            return;
        }
        if (!target.trySpendMoney(stake)) {
            challenger.giveMoney(stake);
            toast(target, `You cannot cover the ${formatMoney(stake)} stake.`, {tone: "bad"});
            toast(challenger, `${target.nickname} could not cover the stake.`, {tone: "bad"});
            return;
        }
    }
    const id = nextDuelId++;
    const duel = {id, world: FIRST_DUEL_WORLD + id, firstTo, stake, kit, round: 0, startedAt: Date.now(),
        generation: World.getMissionGeneration(), ending: false,
        sides: [challenger, target].map((player) => ({player, score: 0, loadout: life.loadoutOf(player),
            spot: {position: plain(player.position), heading: headingOf(player.rotation)}}))};
    duels.set(id, duel);
    for (const side of duel.sides) {
        const rival = duel.sides.find((other) => other !== side).player;
        life.begin(side.player, "duel", `Duel vs ${rival.nickname}`);
        life.setOverride(side.player, {spawn: () => side.spot, kit: KITS[kit], delayMs: ROUND_RESET_MS + 1000});
        side.player.setVirtualWorld(duel.world);
        toast(side.player, `Duel against ${rival.nickname}. First to ${firstTo} wins${stake ? ` ${formatMoney(stake * 2)}` : ""}.`, {title: "Duel", tone: "bad"});
    }
    feed("duel", [name(challenger, COLORS.gold), text(" and "), name(target, COLORS.gold), text(` started a duel · ${KITS[kit].label}${stake ? ` · ${formatMoney(stake * 2)} pot` : ""}`)]);
    resetRound(duel);
}

// leavingId names a player who is disconnecting and must not be moved.
function finish(duel, winner, reason, leavingId = null) {
    if (!duels.delete(duel.id)) return;
    for (const side of duel.sides) {
        const player = side.player;
        life.clearOverride(player);
        life.end(player, "duel");
        if (player.id === leavingId || !World.getPlayers().some((other) => other.id === player.id)) continue;
        card(player, "duel", null);
        player.setVirtualWorld(MAIN_WORLD);
        placeUntil(player, side.spot, side.loadout, () => World.getMissionGeneration() === duel.generation && !duelOf(player));
    }
    if (!winner) {
        for (const side of duel.sides) {
            if (duel.stake) side.player.giveMoney(duel.stake);
            toast(side.player, reason ?? "The duel ended in a draw. Stakes returned.");
        }
        return;
    }
    const loser = duel.sides.find((side) => side.player.id !== winner.id).player;
    if (duel.stake) winner.giveMoney(duel.stake * 2);
    profiles.bump(winner, "duelsWon");
    profiles.addXp(winner, DUEL_XP, `Beat ${loser.nickname}`);
    toast(winner, `You won the duel${duel.stake ? ` and ${formatMoney(duel.stake * 2)}` : ""}.`, {title: "Duel", tone: "gold"});
    toast(loser, reason ?? `${winner.nickname} won the duel.`, {title: "Duel", tone: "bad"});
    const score = duel.sides.map((side) => side.score).sort((a, b) => b - a).join("–");
    feed("duel", [name(winner, COLORS.gold), text(" beat "), name(loser), text(` in a duel · ${score}`)]);
}

Events.on("playerDeath", (victim) => {
    const duel = duelOf(victim);
    if (!duel || duel.ending) return;
    const winnerSide = duel.sides.find((side) => side.player.id !== victim.id);
    winnerSide.score++;
    showCards(duel);
    if (winnerSide.score >= duel.firstTo) {
        duel.ending = true;
        for (const side of duel.sides) announce(side.player, `${winnerSide.player.nickname.toUpperCase()} WINS`, 3);
        setTimeout(() => finish(duel, winnerSide.player), ROUND_RESET_MS);
        return;
    }
    setTimeout(() => resetRound(duel), ROUND_RESET_MS);
});

Events.on("playerDisconnect", (player) => {
    const duel = duelOf(player);
    if (!duel) return;
    const other = duel.sides.find((side) => side.player.id !== player.id).player;
    finish(duel, other, `${player.nickname} left. You win by forfeit.`, player.id);
});

Events.on("missionChange", () => {
    for (const duel of [...duels.values()]) finish(duel, null, "The duel was called off as the city changed.");
});

let limitTimer = null;
onStart(() => {
    limitTimer = setInterval(() => {
        for (const duel of [...duels.values()]) {
            if (Date.now() - duel.startedAt > DUEL_LIMIT_MS && !duel.ending) finish(duel, null, "Time ran out. The duel is a draw; stakes returned.");
        }
    }, 5000);
});
onStop(() => {
    clearInterval(limitTimer);
    for (const duel of [...duels.values()]) finish(duel, null, "The duel was called off.");
});

commands.register({name: "duel", usage: "/duel <player> [first-to 1-5] [stake] [colt|shotgun|thompson|bat]",
    help: "Challenge someone nearby to a private duel.", category: "Combat"}, (player, args) => {
    if (!args.length) {
        toast(player, "Use /duel <player> [rounds] [stake] [weapon]. Example: /duel Paulie 3 200 thompson");
        return;
    }
    if (args[0].toLowerCase() === "leave" || args[0].toLowerCase() === "forfeit") {
        const duel = duelOf(player);
        if (duel && !duel.ending) finish(duel, duel.sides.find((side) => side.player.id !== player.id).player, "You forfeited the duel.");
        return;
    }
    const target = findPlayer(args[0]);
    const rest = args.slice(1).map((arg) => arg.toLowerCase());
    const kit = rest.find((arg) => KITS[arg]) ?? "colt";
    const numbers = rest.filter((arg) => /^\d+$/.test(arg)).map(Number);
    const firstTo = Math.min(5, Math.max(1, numbers[0] ?? 3));
    const stake = Math.min(MAX_STAKE, numbers[1] ?? 0);
    if (!target || target.id === player.id) {
        toast(player, "Nobody by that name is in town.", {tone: "bad"});
        return;
    }
    const own = eligible(player, null);
    const theirs = own ? null : eligible(target, player);
    if (own || theirs) {
        toast(player, own ? explain(player, player, own) : explain(player, target, theirs), {tone: "bad"});
        return;
    }
    invites.offer(target, {kind: "duel", from: player,
        summary: `Duel · first to ${firstTo} · ${KITS[kit].label}${stake ? ` · ${formatMoney(stake)} each` : ""}`,
        text: `${player.nickname} challenges you to a duel: first to ${firstTo}, ${KITS[kit].label}${stake ? `, ${formatMoney(stake)} each` : ""}.`,
        accept: (accepter, challenger) => begin(challenger, accepter, firstTo, stake, kit)});
    toast(player, `Challenge sent to ${target.nickname}.`);
});
