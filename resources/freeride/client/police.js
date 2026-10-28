// Shows what server/police.js decides: the retail police lines, the wanted
// badge and the arrest fade. Fines and bail come out of the cash balance,
// which the score counter mirrors. On police duty it also offers the call
// box under the player's feet (server/police_backup.js) and plays the
// telephone and radio sounds of a call; the server checks every call.
const BOOTH_PROMPT_COLOR = 0xffe7c47d;
const BOOTH_PROMPT_FILL = 0xd0121519;
const CALL_KEY = "u";

const callBoxes = {reach: 0, booths: []};

function boothInReach(player) {
    if (!callBoxes.booths.length || !player?.spawned || !player.alive || player.getVehicle()) return null;
    const position = player.position;
    return callBoxes.booths.find((booth) => Math.hypot(booth.x - position.x, booth.z - position.z) <= callBoxes.reach &&
        Math.abs(booth.y - position.y) <= 2.5) ?? null;
}

Events.on("fr:police", (data) => {
    switch (data?.action) {
    case "text":
        Hud.showGameText(data.textId);
        break;
    case "wanted":
        Hud.setWanted(data.level ?? 0);
        break;
    case "fine":
        Sound.play("01b_srdce.wav", {volume: 0.5});
        break;
    case "arrest":
        Hud.setWanted(0);
        Fade.out(1);
        setTimeout(() => Fade.in(1), 3500);
        break;
    case "booths":
        callBoxes.reach = Number(data.reach) || 0;
        callBoxes.booths = Array.isArray(data.booths) ? data.booths : [];
        break;
    case "sound":
        if (typeof data.wave === "string") Sound.play(data.wave, {volume: Number(data.volume) || 0.8});
        break;
    }
});

Key.bind(CALL_KEY, () => {
    if (boothInReach(LocalPlayer)) Events.emitServer("fr:backup:call", {});
});

Events.on("render", () => {
    if (!boothInReach(LocalPlayer)) return;
    const {width, height} = Web.getScreenSize();
    const label = "[U] CALL THE PRECINCT FOR BACKUP";
    Draw.rect(width / 2 - 190, height - 96, 380, 36, BOOTH_PROMPT_FILL);
    Draw.text(label, width / 2, height - 72, 15, BOOTH_PROMPT_COLOR, 3);
});

Events.on("missionUnload", () => { callBoxes.booths = []; });
