// Pending requests between players: duels, rides, crew invitations. The
// target answers with /accept or /decline, from chat or the city menu.
const commands = require("./commands.js");
const {toast, card} = require("./notify.js");
const {playerById} = require("./util.js");

const INVITE_MS = 30_000;
const pending = new Map();
let nextId = 1;

function listFor(player) {
    const now = Date.now();
    const list = (pending.get(player.id) ?? []).filter((invite) => invite.expiresAt > now && playerById(invite.fromId));
    pending.set(player.id, list);
    return list;
}

function showCard(player) {
    const list = listFor(player);
    card(player, "invites", list.length ? {
        title: list.length === 1 ? "Invitation" : `${list.length} invitations`,
        subtitle: "Type /accept or /decline, or use the F1 menu",
        rows: list.map((invite) => [invite.fromName, invite.summary]), tone: "gold",
    } : null);
}

// invite: {kind, from, summary, text, accept(), decline?()}
function offer(target, invite) {
    const list = listFor(target).filter((entry) => !(entry.kind === invite.kind && entry.fromId === invite.from.id));
    const entry = {id: nextId++, kind: invite.kind, fromId: invite.from.id, fromName: invite.from.nickname,
        summary: invite.summary, accept: invite.accept, decline: invite.decline, expiresAt: Date.now() + INVITE_MS};
    list.push(entry);
    pending.set(target.id, list);
    toast(target, `${invite.text} /accept or /decline.`, {title: invite.from.nickname, tone: "gold", seconds: 10});
    showCard(target);
    setTimeout(() => showCard(target), INVITE_MS + 100);
    return entry.id;
}

function take(player, args) {
    const list = listFor(player);
    const query = args.join(" ").trim().toLowerCase();
    const index = query ? list.findIndex((invite) => invite.fromName.toLowerCase().startsWith(query) || String(invite.id) === query)
        : list.length - 1;
    if (index < 0) return null;
    const [invite] = list.splice(index, 1);
    showCard(player);
    return invite;
}

function withdraw(from, kind) {
    for (const [targetId, list] of pending) {
        const kept = list.filter((invite) => !(invite.fromId === from.id && invite.kind === kind));
        if (kept.length !== list.length) {
            pending.set(targetId, kept);
            const target = playerById(targetId);
            if (target) showCard(target);
        }
    }
}

commands.register({name: "accept", aliases: ["yes"], usage: "/accept [player]", help: "Accept the latest invitation.", category: "Social"}, (player, args) => {
    const invite = take(player, args);
    if (!invite) {
        toast(player, "No invitation is waiting.");
        return;
    }
    const from = playerById(invite.fromId);
    if (from) invite.accept(player, from);
});

commands.register({name: "decline", aliases: ["no"], usage: "/decline [player]", help: "Turn down the latest invitation.", category: "Social"}, (player, args) => {
    const invite = take(player, args);
    if (!invite) return;
    const from = playerById(invite.fromId);
    if (from) {
        toast(from, `${player.nickname} declined.`);
        invite.decline?.(player, from);
    }
});

Events.on("playerDisconnect", (player) => {
    pending.delete(player.id);
    for (const [targetId, list] of pending) pending.set(targetId, list.filter((invite) => invite.fromId !== player.id));
});

module.exports = {offer, withdraw, listFor: (player) => listFor(player).map(({id, kind, fromName, summary}) => ({id, kind, from: fromName, summary}))};
