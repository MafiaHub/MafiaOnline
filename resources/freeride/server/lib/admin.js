// Server admins. Free Ride has no accounts, so an admin is a player record
// (nickname on one machine, see profiles.js) granted from the server console:
// "fr admin add <player>", "fr admin remove <player>", "fr admin list".
const fs = require("node:fs");
const path = require("node:path");
const profiles = require("./profiles.js");

const FILE = path.join(profiles.DATA_DIR, "admins.json");
let admins = new Set();

function load() {
    try {
        admins = new Set(JSON.parse(fs.readFileSync(FILE, "utf8")).admins ?? []);
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not read ${FILE}: ${error.message}`);
    }
}

function save() {
    try {
        fs.mkdirSync(profiles.DATA_DIR, {recursive: true});
        fs.writeFileSync(`${FILE}.tmp`, JSON.stringify({version: 1, admins: [...admins]}, null, 1));
        fs.renameSync(`${FILE}.tmp`, FILE);
    } catch (error) {
        console.error(`[freeride] Could not save admins: ${error.message}`);
    }
}

function isAdmin(player) {
    const key = profiles.keyOf(player);
    return Boolean(key && admins.has(key));
}

function set(key, admin) {
    if (admin) admins.add(key);
    else admins.delete(key);
    save();
}

load();

module.exports = {isAdmin, set, list: () => [...admins]};
