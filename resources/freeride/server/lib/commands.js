// One command table for chat and the F2 city menu. A menu button sends the
// same text a player could type, so both paths share every server check.
const {toast} = require("./notify.js");
const profiles = require("./profiles.js");
const {inFreeRide} = require("./util.js");

const CATEGORY_ORDER = ["City", "Cars", "Races", "Combat", "Crew", "Social", "Showcase", "Dev"];
const commands = new Map();
const aliases = new Map();

function register(spec, handler) {
    const entry = {name: spec.name, usage: spec.usage ?? `/${spec.name}`, help: spec.help ?? "",
        category: spec.category ?? "City", hidden: Boolean(spec.hidden), handler};
    commands.set(spec.name, entry);
    for (const alias of spec.aliases ?? []) aliases.set(alias, spec.name);
}

function resolve(name) {
    const key = name.toLowerCase();
    return commands.get(key) ?? commands.get(aliases.get(key));
}

function run(player, command, args) {
    // On a Free Ride map a player's record is attached at login; nothing
    // runs before it.
    if (inFreeRide() && !profiles.get(player)) {
        player.sendMessage("Log in first.", 0xd0574a);
        return;
    }
    const entry = resolve(command);
    if (!entry) {
        toast(player, `Unknown command /${command}. Press F2 for the city menu or type /help.`, {tone: "bad"});
        return;
    }
    try {
        entry.handler(player, args.map((arg) => String(arg)));
    } catch (error) {
        console.error(`[freeride] /${entry.name} failed for ${player.nickname}: ${error?.stack ?? error}`);
        toast(player, "That did not work. The city is looking into it.", {tone: "bad"});
    }
}

// Splits a menu command line the way chat does.
function runText(player, text) {
    if (typeof text !== "string" || !text.startsWith("/") || text.length > 160) return;
    const [command, ...args] = text.slice(1).trim().split(/\s+/);
    if (command) run(player, command, args);
}

function list({includeHidden = false} = {}) {
    return [...commands.values()]
        .filter((entry) => includeHidden || !entry.hidden)
        .sort((a, b) => CATEGORY_ORDER.indexOf(a.category) - CATEGORY_ORDER.indexOf(b.category) || a.name.localeCompare(b.name))
        .map(({name, usage, help, category}) => ({name, usage, help, category}));
}

Events.on("playerCommand", (player, command, args) => run(player, command, args));

module.exports = {register, run, runText, list};
