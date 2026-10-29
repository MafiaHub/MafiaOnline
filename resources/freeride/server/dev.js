// Development commands for testing combat, cars and the HUD. They are off on
// a public server; see lib/devmode.js for turning them on.
const commands = require("./lib/commands.js");
const devmode = require("./lib/devmode.js");
const {toast} = require("./lib/notify.js");
const {emit, playable, findPlayer, vehicleById, headingOf, onStop} = require("./lib/util.js");

const testObjects = new Map();


function testKit(player, big) {
    const large = big === "shotgun" ? {weaponId: 11, loaded: 8, reserve: 32} : big === "thompson" ? {weaponId: 10, loaded: 50, reserve: 150} : {weaponId: 4};
    return player.setInventory([
        {weaponId: 3},                 // knife: melee replay
        {weaponId: 6, loaded: 6, reserve: 36},
        large,
        {weaponId: 15, loaded: 3},     // grenades
    ], large.weaponId);
}

function lastCar(player) {
    return player.getVehicle() ?? vehicleById(World.getVehicles().filter((vehicle) => vehicle.state.get("fr.owner") === player.nickname).pop()?.id);
}

const actions = {
    weapons(player, [variant = "bat"]) {
        if (!["bat", "shotgun", "thompson"].includes(variant)) return "Use /dev weapons bat|shotgun|thompson";
        return testKit(player, variant) ? `Test kit: knife, Colt, ${variant}, grenades` : "Could not equip the test kit";
    },
    fists(player) {
        return player.setInventory([]) ? "Unarmed" : "Could not holster";
    },
    drop(player, [id]) {
        const weaponId = id === undefined ? undefined : Number(id);
        const pickup = weaponId === undefined ? player.dropWeapon() : player.dropWeapon(weaponId);
        return pickup ? `Dropped weapon ${pickup.weaponId} as pickup ${pickup.id}` : "Weapon not held, or no room for another pickup";
    },
    model(player, [file]) {
        if (!file) return `Current model: ${player.model}`;
        return player.setModel(file) ? `Model set to ${file}` : "Invalid model filename";
    },
    wanted(player, [level]) {
        const value = Number(level);
        return Number.isInteger(value) && Police.setWanted(player, value) ? `Wanted level ${value}` : "Use /dev wanted 0-3 (needs the simulation host)";
    },
    hud(player, [action = "sound", value]) {
        emit(player, "fr:dev:hud", {action, value});
        return `HUD test: ${action}`;
    },
    opacity(player, [value]) {
        const car = lastCar(player);
        const opacity = Number(value);
        return car && Number.isFinite(opacity) && car.setOpacity(opacity) ? `Opacity ${opacity}` : "Use /dev opacity 0-1 near your car";
    },
    carstate(player) {
        const car = lastCar(player);
        if (!car) return "No car";
        const {x, y, z} = car.velocity;
        return `Car ${car.id}: ${Math.round(Math.hypot(x, y, z) * 3.6)} km/h, gear ${car.gear}, fuel ${car.fuel.toFixed(1)}/${car.fuelTankCapacity.toFixed(1)}, engine ${car.engineOn ? "on" : "off"}`;
    },
    cardamage(player) {
        const damage = lastCar(player)?.getDamageState();
        if (!damage) return "No native damage reported yet";
        const broken = (parts, bit) => parts.filter((part) => (part.flags & bit) !== 0).length;
        return `Damage revision ${damage.revision}, body ${damage.bodyDamage.toFixed(1)}, glass/parts ${broken(damage.zones, 1)}, tires ${broken(damage.wheels, 0x400)}`;
    },
    // Straight to a cell, as after an arrest at this wanted level.
    jail(player, [level = "1"]) {
        return require("./jail.js").sentence(player, Math.max(1, Number(level) || 1)) ? "Locked up" : "Could not lock you up here";
    },
    // A map object four metres ahead, for collision tests; the next one replaces it.
    object(player, [model]) {
        if (typeof MapObject === "undefined") return "This server has no MapObject";
        if (!model) return "Use /dev object <model.4ds> (off removes it)";
        testObjects.get(player.id)?.destroy();
        testObjects.delete(player.id);
        if (model === "off") return "Removed";
        const heading = headingOf(player.rotation);
        const {x, y, z} = player.position;
        const object = MapObject.create(model, {x: x + Math.sin(heading) * 4, y, z: z + Math.cos(heading) * 4}, {x: 0, y: heading, z: 0},
            {virtualWorld: player.virtualWorld});
        if (!object) return `Could not create ${model}`;
        testObjects.set(player.id, object);
        return `Object ${object.id}: ${model}`;
    },
    money(player, [amount, who]) {
        const target = who ? findPlayer(who) : player;
        return target && Number.isInteger(Number(amount)) && target.giveMoney(Number(amount)) ? `${target.nickname} has $${target.money}` : "Use /dev money <amount> [player]";
    },
};

commands.register({name: "dev", usage: "/dev weapons|fists|drop|model|wanted|hud|opacity|carstate|cardamage|jail|object|money",
    help: "Development tools (FREERIDE_DEV=1 or console: fr dev on).", category: "Dev", hidden: true}, (player, [action, ...args]) => {
    if (!devmode.isOn()) {
        toast(player, "Development commands are off on this server.", {tone: "bad"});
        return;
    }
    if (!actions[action]) {
        player.sendMessage(`/dev ${Object.keys(actions).join("|")}`, 0xd9cdb4);
        return;
    }
    if (!playable(player) && action !== "hud") return;
    player.sendMessage(actions[action](player, args), 0xd9cdb4);
});

Events.on("playerDisconnect", (player) => {
    testObjects.get(player.id)?.destroy();
    testObjects.delete(player.id);
});
onStop(() => {
    for (const object of testObjects.values()) object.destroy();
    testObjects.clear();
});

// The server console runs any action on a player: fr as <player> <action> ...
function run(player, action, args) {
    return actions[action] ? actions[action](player, args) : `Actions: ${Object.keys(actions).join("|")}`;
}

module.exports = {run};
