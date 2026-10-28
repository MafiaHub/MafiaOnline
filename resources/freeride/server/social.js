// Getting together: private messages, asking for a ride to a friend, and
// watching another player's camera.
const life = require("./lib/life.js");
const invites = require("./lib/invites.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, playable, findPlayer, headingOf, formatMoney} = require("./lib/util.js");

const GOTO_COOLDOWN_MS = 60_000;
const RECENT_DAMAGE_MS = 15_000;

const lastDamage = new Map();
const lastGoto = new Map();

function blocked(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return "You need to be out on the street.";
    if (life.activity(player)) return life.busyMessage(player);
    if ((Police.getRecord(player)?.level ?? 0) > 0) return "Not with the police after you.";
    if (Date.now() - (lastDamage.get(player.id) ?? 0) < RECENT_DAMAGE_MS) return "Not in the middle of a fight.";
    return null;
}

function bring(traveller, host) {
    const problem = blocked(traveller) ?? (playable(host) && host.virtualWorld === MAIN_WORLD ? null : `${host.nickname} is not on the street.`);
    if (problem) {
        toast(traveller, problem, {tone: "bad"});
        toast(host, `${traveller.nickname} could not come: ${problem}`, {tone: "bad"});
        return;
    }
    const car = host.getVehicle();
    const seat = car ? car.getOccupants().findIndex((occupant) => occupant === null) : -1;
    const heading = headingOf(host.rotation);
    const {x, y, z} = car ? car.position : host.position;
    const behind = car ? 4 : 1.6;
    if (traveller.getVehicle()) traveller.removeFromVehicle();
    if (!life.teleport(traveller, {x: x - Math.sin(heading) * behind, y: y + 0.2, z: z - Math.cos(heading) * behind}, heading)) {
        toast(traveller, "The ride fell through.", {tone: "bad"});
        return;
    }
    lastGoto.set(traveller.id, Date.now());
    if (car && seat > 0) traveller.putInVehicle(car, seat);
    toast(traveller, `You caught up with ${host.nickname}.`, {tone: "good"});
    toast(host, `${traveller.nickname} is here.`, {tone: "good"});
}

commands.register({name: "goto", aliases: ["tp"], usage: "/goto <player>", help: "Ask a player to bring you to them.", category: "Social"}, (player, args) => {
    const target = findPlayer(args.join(" "));
    if (!target || target.id === player.id) {
        toast(player, "Nobody by that name is in town.", {tone: "bad"});
        return;
    }
    const problem = blocked(player);
    if (problem) {
        toast(player, problem, {tone: "bad"});
        return;
    }
    const wait = (lastGoto.get(player.id) ?? 0) + GOTO_COOLDOWN_MS - Date.now();
    if (wait > 0) {
        toast(player, `Your driver needs ${Math.ceil(wait / 1000)} more seconds.`, {tone: "bad"});
        return;
    }
    invites.offer(target, {kind: "goto", from: player, summary: "Wants to come to you",
        text: `${player.nickname} wants to come to you.`, accept: (host, traveller) => bring(traveller, host)});
    toast(player, `Asked ${target.nickname} for a ride.`);
});

const DICE_REACH = 15;
const MIN_BET = 10;
const MAX_BET = 1000;

function roll() {
    return 2 + Math.floor(Math.random() * 6) + Math.floor(Math.random() * 6);
}

// Both players put the bet down; the server rolls two dice for each.
function playDice(challenger, opponent, bet) {
    const near = playable(challenger) && playable(opponent) && distance(challenger.position, opponent.position) <= DICE_REACH;
    if (!near) {
        toast(challenger, "You need to be at the same table.", {tone: "bad"});
        toast(opponent, "You need to be at the same table.", {tone: "bad"});
        return;
    }
    if (!challenger.trySpendMoney(bet)) {
        toast(opponent, `${challenger.nickname} cannot cover the bet.`, {tone: "bad"});
        return;
    }
    if (!opponent.trySpendMoney(bet)) {
        challenger.giveMoney(bet);
        toast(challenger, `${opponent.nickname} cannot cover the bet.`, {tone: "bad"});
        return;
    }
    const a = roll();
    const b = roll();
    const line = `${challenger.nickname} rolls ${a}, ${opponent.nickname} rolls ${b}.`;
    if (a === b) {
        challenger.giveMoney(bet);
        opponent.giveMoney(bet);
        for (const player of [challenger, opponent]) toast(player, `${line} A push; bets back.`, {title: "Dice"});
        return;
    }
    const [winner, loser] = a > b ? [challenger, opponent] : [opponent, challenger];
    winner.giveMoney(bet * 2);
    toast(winner, `${line} You take ${formatMoney(bet * 2)}.`, {title: "Dice", tone: "good"});
    toast(loser, `${line} ${winner.nickname} takes the pot.`, {title: "Dice", tone: "bad"});
    feed("dice", [name(winner, COLORS.gold), text(` won ${formatMoney(bet * 2)} at dice from `), name(loser)]);
}

commands.register({name: "dice", usage: "/dice <player> <bet>", help: "Bet someone nearby on a roll of the dice.", category: "Social"}, (player, args) => {
    const target = findPlayer(args[0]);
    const bet = Math.floor(Number(args[1]));
    if (!target || target.id === player.id || !Number.isFinite(bet) || bet < MIN_BET || bet > MAX_BET) {
        toast(player, `Use /dice <player> <bet from ${MIN_BET} to ${MAX_BET}>.`, {tone: "bad"});
        return;
    }
    if (player.money < bet) {
        toast(player, "You cannot cover that bet.", {tone: "bad"});
        return;
    }
    invites.offer(target, {kind: "dice", from: player, summary: `Dice for ${formatMoney(bet)}`,
        text: `${player.nickname} bets you ${formatMoney(bet)} on a roll of the dice.`, accept: (opponent, challenger) => playDice(challenger, opponent, bet)});
    toast(player, `Bet offered to ${target.nickname}.`);
});

commands.register({name: "pm", aliases: ["msg", "w"], usage: "/pm <player> <message>", help: "Send a private message.", category: "Social"}, (player, args) => {
    const target = findPlayer(args[0]);
    const message = args.slice(1).join(" ").trim().slice(0, 120);
    if (!target || !message) {
        toast(player, "Use /pm <player> <message>.", {tone: "bad"});
        return;
    }
    target.sendMessage(`[from ${player.nickname}] ${message}`, 0xc9a0e8);
    player.sendMessage(`[to ${target.nickname}] ${message}`, 0x9d86b0);
});

commands.register({name: "spectate", aliases: ["follow", "watch"], usage: "/spectate <player|off>", help: "Watch through another player's camera.", category: "Social"}, (player, args) => {
    const query = args.join(" ").trim();
    if (!query || query.toLowerCase() === "off") {
        if (player.setCameraTarget(null)) toast(player, "Back to your own eyes.");
        return;
    }
    const target = findPlayer(query);
    if (!target || target.id === player.id) {
        toast(player, "Nobody by that name is in town.", {tone: "bad"});
        return;
    }
    toast(player, player.setCameraTarget(target) ? `Watching ${target.nickname}. /spectate off to stop.` : `Cannot watch ${target.nickname} right now.`);
});

commands.register({name: "players", aliases: ["who"], usage: "/players", help: "List who is in town.", category: "Social"}, (player) => {
    const list = World.getPlayers().map((other) => `${other.nickname} (${other.ping} ms)`);
    player.sendMessage(`${list.length} in town: ${list.join(", ")}`, 0xd9cdb4);
});

menu.provide("players", (viewer) => ({
    list: World.getPlayers().map((player) => ({
        id: player.id, name: player.nickname, you: player.id === viewer.id, ping: player.ping,
        level: player.state.get("fr.level") ?? 1, rank: player.state.get("fr.rank") ?? "", crew: player.state.get("fr.crew") ?? null,
        activity: player.state.get("fr.activity") ?? null, bounty: player.state.get("fr.bounty") ?? null, spawned: player.spawned,
    })),
    invites: invites.listFor(viewer),
}));

// Everyone's HUD lists who is talking on proximity voice.
Events.on("playerVoiceStart", (player) => Events.emitAllClients("fr:voice", {id: player.id, name: player.nickname, talking: true}));
Events.on("playerVoiceStop", (player) => Events.emitAllClients("fr:voice", {id: player.id, name: player.nickname, talking: false}));

Events.on("playerDamage", (victim, {attacker}) => {
    lastDamage.set(victim.id, Date.now());
    if (attacker) lastDamage.set(attacker.id, Date.now());
});
Events.on("playerDisconnect", (player) => {
    lastDamage.delete(player.id);
    lastGoto.delete(player.id);
});
