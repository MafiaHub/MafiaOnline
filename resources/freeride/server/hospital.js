// Lost Heaven Hospital, on maps that have one (lib/maps.js). The dead wake up
// at its doors instead of their district, and the hospital sends a bill.
// The Dispensary's attendant stands at the same doors and sells bandages.
const maps = require("./lib/maps.js");
const life = require("./lib/life.js");
const {toast, markers, COLORS} = require("./lib/notify.js");
const {formatMoney} = require("./lib/util.js");

const BILL = 50;

function hospital() {
    return maps.current()?.hospital ?? null;
}

// lib/life.js asks this before the player's district.
life.setHospital(() => hospital()?.spawn ?? null);

function marker(player) {
    const place = hospital();
    markers(player, "hospital", place ? [{id: "hospital", position: place.door, label: place.name.toUpperCase(), color: COLORS.green, range: 150, map: false}] : []);
}

Events.on("playerSpawn", marker);
Events.on("playerRespawn", (player) => {
    const place = hospital();
    if (!place || life.isTransit(player) || life.activity(player)) return;
    const bill = Math.min(player.money, BILL);
    if (bill > 0) player.giveMoney(-bill);
    toast(player, `The doctors patched you up${bill ? ` and billed you ${formatMoney(bill)}` : ""}.`, {title: place.name});
});
