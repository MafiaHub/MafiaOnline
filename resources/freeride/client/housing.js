// The home decorating keys (server/housing.js). Only while the server says
// the player is decorating: Home/End (or numpad 4/6) turn the piece being
// carried, PgUp/PgDn (or numpad 8/2) raise and lower it, Delete sells it back
// and numpad 0 puts it back, or ends decorating when nothing is carried. The
// mouse wheel is left alone: the game switches weapons with it. G, which picks
// up and places, is the job key (client/rescue.js).
let decorating = false;

function send(request) {
    if (decorating) Events.emitServer("fr:home:edit", request);
}

Events.on("fr:home:edit", (data) => {
    decorating = Boolean(data?.on);
});

for (const [key, request] of [
    ["end", {turn: 1}], ["home", {turn: -1}], ["numpad6", {turn: 1}], ["numpad4", {turn: -1}],
    ["pageup", {lift: 1}], ["pagedown", {lift: -1}], ["numpad8", {lift: 1}], ["numpad2", {lift: -1}],
    ["delete", {sell: true}], ["numpad0", {cancel: true}],
]) {
    Key.bind(key, () => send(request));
}

Events.on("missionUnload", () => {
    decorating = false;
});
