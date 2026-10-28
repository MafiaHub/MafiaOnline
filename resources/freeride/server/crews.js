// Crews: a tag, a colour and a private radio. Members wear the tag on their
// nametag and car marker, see each other across the city and talk on /c.
// Crews persist between sessions; membership follows the nickname.
const store = require("./lib/store.js");
const profiles = require("./lib/profiles.js");
const invites = require("./lib/invites.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const nametag = require("./lib/nametag.js");
const {toast, feed, name, text, COLORS} = require("./lib/notify.js");
const {findPlayer, onStart} = require("./lib/util.js");

const MAX_MEMBERS = 8;
const MAX_CREWS = 50;
// No red: a red nametag means a bounty.
const PALETTE = ["#5f9fd6", "#8fc27a", "#c98bd8", "#e0a44f", "#59c2b5", "#e27fa1", "#c9c26a", "#9aa6f0"];

let crews = [];

function key(nickname) {
    return nickname.toLowerCase();
}

function load() {
    crews = store.load("crews")?.crews ?? [];
}

function save() {
    store.save("crews", {version: 1, crews});
}

function crewOf(player) {
    return crews.find((crew) => crew.members.includes(key(player.nickname))) ?? null;
}

// Members are stored by lower-case nickname; `names` keeps how they spell it.
function display(crew, member) {
    return crew.names?.[member] ?? member;
}

function remember(crew, player) {
    crew.names ??= {};
    crew.names[key(player.nickname)] = player.nickname;
}

function onlineMembers(crew) {
    return World.getPlayers().filter((player) => crew.members.includes(key(player.nickname)));
}

function apply(player) {
    const crew = crewOf(player);
    profiles.setPublic(player, "fr.crew", crew ? {tag: crew.tag, name: crew.name, color: crew.color} : null);
    nametag.refresh(player);
}

function radio(crew, line, color = 0xe8c57d) {
    for (const member of onlineMembers(crew)) member.sendMessage(`[${crew.tag}] ${line}`, color);
}

function join(player, crew) {
    if (crewOf(player)) {
        toast(player, "Leave your current crew first.", {tone: "bad"});
        return;
    }
    if (crew.members.length >= MAX_MEMBERS) {
        toast(player, `${crew.name} is full.`, {tone: "bad"});
        return;
    }
    crew.members.push(key(player.nickname));
    remember(crew, player);
    save();
    apply(player);
    radio(crew, `${player.nickname} joined the crew.`);
}

function leave(player) {
    const crew = crewOf(player);
    if (!crew) return;
    crew.members = crew.members.filter((member) => member !== key(player.nickname));
    if (!crew.members.length) {
        crews = crews.filter((entry) => entry !== crew);
        feed("crew", [text(`[${crew.tag}] ${crew.name}`, crew.color), text(" disbanded")]);
    } else if (crew.leader === key(player.nickname)) {
        crew.leader = crew.members[0];
        radio(crew, `${player.nickname} left. ${display(crew, crew.leader)} leads the crew now.`);
    } else {
        radio(crew, `${player.nickname} left the crew.`);
    }
    save();
    apply(player);
    toast(player, `You left ${crew.name}.`);
}

const subcommands = {
    create(player, args) {
        const tag = (args[0] ?? "").toUpperCase();
        const crewName = args.slice(1).join(" ").trim() || tag;
        if (!/^[A-Z0-9]{2,4}$/.test(tag) || !/^[A-Za-z0-9 '.-]{2,24}$/.test(crewName)) {
            toast(player, "Use /crew create <TAG> [name]: a 2-4 letter tag and a name of up to 24 letters.", {tone: "bad"});
            return;
        }
        if (crewOf(player)) {
            toast(player, "Leave your current crew first.", {tone: "bad"});
            return;
        }
        if (crews.some((crew) => crew.tag === tag) || crews.length >= MAX_CREWS) {
            toast(player, "That tag is taken.", {tone: "bad"});
            return;
        }
        const used = new Set(crews.map((crew) => crew.color));
        const color = PALETTE.find((entry) => !used.has(entry)) ?? PALETTE[crews.length % PALETTE.length];
        const crew = {tag, name: crewName, color, leader: key(player.nickname), members: [key(player.nickname)], created: Date.now()};
        remember(crew, player);
        crews.push(crew);
        save();
        apply(player);
        toast(player, `${crewName} is yours. Bring people in with /crew invite <name>.`, {title: `[${tag}]`, tone: "gold"});
        feed("crew", [name(player, COLORS.gold), text(" founded "), text(`[${tag}] ${crewName}`, color)]);
    },
    invite(player, args) {
        const crew = crewOf(player);
        const target = findPlayer(args.join(" "));
        if (!crew) {
            toast(player, "You are not in a crew. /crew create <TAG> starts one.", {tone: "bad"});
            return;
        }
        if (!target || target.id === player.id || crewOf(target)) {
            toast(player, target ? `${target.nickname} already runs with a crew.` : "Nobody by that name is in town.", {tone: "bad"});
            return;
        }
        invites.offer(target, {kind: "crew", from: player, summary: `Join [${crew.tag}] ${crew.name}`,
            text: `${player.nickname} invites you to join [${crew.tag}] ${crew.name}.`,
            accept: (accepter) => {
                const current = crews.find((entry) => entry.tag === crew.tag);
                if (current) join(accepter, current);
            }});
        toast(player, `Invitation sent to ${target.nickname}.`);
    },
    leave,
    kick(player, args) {
        const crew = crewOf(player);
        const target = key(args.join(" ").trim());
        if (!crew || crew.leader !== key(player.nickname) || target === key(player.nickname) || !crew.members.includes(target)) {
            toast(player, "Only the crew leader can kick, and only a member.", {tone: "bad"});
            return;
        }
        crew.members = crew.members.filter((member) => member !== target);
        save();
        const online = World.getPlayers().find((other) => key(other.nickname) === target);
        if (online) {
            apply(online);
            toast(online, `You were removed from ${crew.name}.`, {tone: "bad"});
        }
        radio(crew, `${display(crew, target)} was removed from the crew.`);
    },
    info(player) {
        const crew = crewOf(player);
        if (!crew) {
            toast(player, "You are not in a crew. /crew create <TAG> [name] starts one.");
            return;
        }
        const online = new Set(onlineMembers(crew).map((member) => key(member.nickname)));
        const members = crew.members.map((member) => `${display(crew, member)}${online.has(member) ? "*" : ""}`);
        player.sendMessage(`[${crew.tag}] ${crew.name} · leader ${display(crew, crew.leader)} · ${members.join(", ")}`, 0xe8c57d);
    },
};

commands.register({name: "crew", aliases: ["gang"], usage: "/crew [create|invite|leave|kick|info]", help: "Found or manage a crew.", category: "Crew"}, (player, args) => {
    const action = (args[0] ?? "info").toLowerCase();
    (subcommands[action] ?? subcommands.info)(player, args.slice(1));
});

commands.register({name: "c", usage: "/c <message>", help: "Talk on your crew radio.", category: "Crew"}, (player, args) => {
    const crew = crewOf(player);
    const message = args.join(" ").trim().slice(0, 120);
    if (!crew || !message) {
        toast(player, crew ? "Say something: /c <message>" : "You are not in a crew.", {tone: "bad"});
        return;
    }
    radio(crew, `${player.nickname}: ${message}`, parseInt(crew.color.slice(1), 16));
});

menu.provide("crew", (player) => {
    const crew = crewOf(player);
    if (!crew) return {crew: null, crews: crews.map(({tag, name, color, members}) => ({tag, name, color, members: members.length}))};
    const online = new Set(onlineMembers(crew).map((member) => key(member.nickname)));
    return {crew: {tag: crew.tag, name: crew.name, color: crew.color, leader: display(crew, crew.leader), isLeader: crew.leader === key(player.nickname),
        members: crew.members.map((member) => ({name: display(crew, member), online: online.has(member)}))},
    crews: crews.map(({tag, name, color, members}) => ({tag, name, color, members: members.length}))};
});

Events.on("playerConnect", apply);
onStart(() => {
    load();
    for (const player of World.getPlayers()) apply(player);
});
