// The job key (G, client/rescue.js): sign on or off under a station's
// arrow (lib/stations.js), spray the hose, lead a trapped civilian out,
// treat a patient, give up waiting for a medic. The client reports the key
// down, keeps pinging while it is held and reports it up; a held key nobody pinged for a while counts
// as released, so a chat window that swallowed the key-up cannot leave a
// hose running. Modules register in order; the first that takes a press owns
// it until the key goes up.
const {onClient, emit, onStop} = require("./util.js");

const PING_TIMEOUT_MS = 800;

const handlers = [];
const held = new Map();

// handler: {press(player) -> boolean, release?(player)}
function register(handler) {
    handlers.push(handler);
}

function release(playerId) {
    const entry = held.get(playerId);
    if (!entry) return;
    held.delete(playerId);
    const player = World.getPlayers().find((candidate) => candidate.id === playerId);
    if (player) entry.owner?.release?.(player);
}

function isHeld(player) {
    return held.has(player.id);
}

// Who may hold the prompt line, strongest first: standing at a station's
// sign-on point beats a job's own hint, which beats a side activity's (the
// rum distillery's) hint.
const PROMPT_LAYERS = ["station", "job", "side"];
const prompts = new Map();

// The prompt line and an optional progress bar above it; null clears this
// layer and shows the next one down.
function prompt(player, text, progress = null, layer = "job") {
    const layers = prompts.get(player.id) ?? {};
    layers[layer] = text ? {text, progress} : null;
    prompts.set(player.id, layers);
    const top = PROMPT_LAYERS.map((name) => layers[name]).find(Boolean) ?? null;
    emit(player, "fr:rescue", {text: top?.text ?? null, progress: top?.progress ?? null});
}

onClient("fr:rescue:use", (player, data) => {
    const down = Boolean(data?.down);
    const entry = held.get(player.id);
    if (!down) {
        release(player.id);
        return;
    }
    if (entry) {
        entry.pingAt = Date.now();
        return;
    }
    const owner = handlers.find((handler) => handler.press(player)) ?? null;
    held.set(player.id, {owner, pingAt: Date.now()});
});

const timer = setInterval(() => {
    const now = Date.now();
    for (const [id, entry] of [...held]) if (now - entry.pingAt > PING_TIMEOUT_MS) release(id);
}, 250);
// A restart loads a fresh copy of this module; the old timer goes with it.
onStop(() => clearInterval(timer));

Events.on("playerDisconnect", (player) => {
    held.delete(player.id);
    prompts.delete(player.id);
});
Events.on("playerDeath", (player) => release(player.id));

module.exports = {register, isHeld, release: (player) => release(player.id), prompt};
