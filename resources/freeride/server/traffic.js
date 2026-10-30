// The city's traffic. The server clears jams on its own (Traffic jam watch,
// mod.trafficJam in server.json); admins can clear the road around them,
// restart the traffic or hand it to a standby simulation host. The console
// has the same: "traffic status | reset <x> <y> <z> [radius] | restart | handover".
const commands = require("./lib/commands.js");
const admin = require("./lib/admin.js");
const {toast} = require("./lib/notify.js");

const DEFAULT_RADIUS = 60;
const MAX_RADIUS = 800;

function status() {
    const host = Traffic.getHost();
    const jam = Traffic.getJamSettings();
    const hostText = host.attached ? `host attached, ${host.standby} standby` : `no host, ${host.standby} standby`;
    const jamText = jam.enabled
        ? `jam watch on: ${jam.minCars} cars within ${jam.radius} m stopped ${jam.stuckSeconds} s`
        : "jam watch off";
    return `Traffic: ${hostText}; ${jamText}.`;
}

function restart(handover) {
    if (Traffic.restartHost(handover)) {
        return handover ? "A standby host takes the traffic over." : "The traffic starts over; riders stepped off the trams.";
    }
    const host = Traffic.getHost();
    if (!host.attached) return "No simulation host runs the traffic.";
    return "No standby simulation host is connected to hand over to.";
}

commands.register({name: "traffic", usage: "/traffic [reset [radius] | restart | handover]", help: "Admins: clear jammed traffic or restart it.", category: "Dev", hidden: true}, (player, args) => {
    if (!admin.isAdmin(player)) {
        toast(player, "Only admins manage the traffic.", {tone: "bad"});
        return;
    }
    const [action = "status", value] = args;
    if (action === "reset") {
        const radius = value === undefined ? DEFAULT_RADIUS : Number(value);
        if (!Number.isFinite(radius) || radius < 0 || radius > MAX_RADIUS) {
            toast(player, `Use /traffic reset [radius 0-${MAX_RADIUS}].`, {tone: "bad"});
            return;
        }
        const origin = player.getVehicle() ?? player;
        const count = Traffic.resetArea(origin.position, radius);
        toast(player, `${count} traffic car${count === 1 ? "" : "s"} within ${radius} m sent off the road.`, {tone: count ? "good" : "info"});
        console.log(`[freeride] ${player.nickname} cleared ${count} traffic cars within ${radius} m`);
    } else if (action === "restart" || action === "handover") {
        const message = restart(action === "handover");
        toast(player, message);
        console.log(`[freeride] ${player.nickname}: traffic ${action}: ${message}`);
    } else {
        toast(player, status());
    }
});

Events.on("consoleCommand", (command, args) => {
    if (command !== "traffic") return;
    const [action = "status", ...rest] = args;
    if (action === "reset") {
        const [x, y, z, radius = DEFAULT_RADIUS] = rest.map(Number);
        if (![x, y, z, radius].every(Number.isFinite) || radius < 0 || radius > MAX_RADIUS) {
            console.log(`[freeride] Usage: traffic reset <x> <y> <z> [radius 0-${MAX_RADIUS}]`);
            return;
        }
        console.log(`[freeride] ${Traffic.resetArea({x, y, z}, radius)} traffic cars sent off the road`);
    } else if (action === "restart" || action === "handover") {
        console.log(`[freeride] ${restart(action === "handover")}`);
    } else {
        console.log(`[freeride] ${status()} Console: traffic status | reset <x> <y> <z> [radius] | restart | handover`);
    }
});

Events.on("trafficJamReset", ({position, vehicles, stuckSeconds}) => {
    console.log(`[freeride] Traffic jam cleared at ${position.x.toFixed(0)}, ${position.y.toFixed(0)}, ${position.z.toFixed(0)}: ${vehicles} cars stood ${Math.round(stuckSeconds)} s`);
});

Events.on("trafficHostChange", ({reason, ejected}) => {
    console.log(`[freeride] Traffic ${reason}${ejected ? `: ${ejected} rider${ejected === 1 ? "" : "s"} taken off the trams` : ""}`);
});

Events.on("playerRailwayEject", (player) => {
    toast(player, "The trams stopped for a moment: you stepped off where you boarded.", {title: "Lost Heaven Transit"});
});
