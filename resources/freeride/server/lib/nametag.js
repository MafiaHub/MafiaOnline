// Nametags show the crew tag and colour, and turn red while a bounty is out.
const defaults = new Map();

function hex(color) {
    return parseInt(color.slice(1), 16);
}

function refresh(player) {
    // getNametagColor reads 0xAARRGGBB; setNametag takes 0xRRGGBB.
    if (!defaults.has(player.id)) defaults.set(player.id, player.getNametagColor() & 0xffffff);
    const crew = player.state.get("fr.crew");
    const bounty = player.state.get("fr.bounty");
    player.setNametag({
        color: bounty ? 0xd0574a : crew ? hex(crew.color) : defaults.get(player.id),
        text: crew ? `[${crew.tag}] ${player.nickname}` : "",
    });
}

// Back to the colour and name the player had before this resource tinted them.
function reset(player) {
    if (defaults.has(player.id)) player.setNametag({color: defaults.get(player.id), text: ""});
    defaults.delete(player.id);
}

Events.on("playerDisconnect", (player) => defaults.delete(player.id));

module.exports = {refresh, reset};
