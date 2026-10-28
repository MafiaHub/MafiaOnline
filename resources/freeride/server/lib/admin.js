// Server admins. Free Ride has no accounts, so an admin is a player record
// (nickname on one machine, see profiles.js) granted from the server console:
// "fr admin add <player>", "fr admin remove <player>", "fr admin list".
const store = require("./store.js");
const profiles = require("./profiles.js");

let admins = new Set();

function load() {
    admins = new Set(store.load("admins")?.admins ?? []);
}

function save() {
    store.save("admins", {version: 1, admins: [...admins]});
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
