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

for (const [key, request, label] of [
    ["end", {turn: 1}, "Decorating: turn right"], ["home", {turn: -1}, "Decorating: turn left"],
    ["numpad6", {turn: 1}, "Decorating: turn right (2nd)"], ["numpad4", {turn: -1}, "Decorating: turn left (2nd)"],
    ["pageup", {lift: 1}, "Decorating: raise"], ["pagedown", {lift: -1}, "Decorating: lower"],
    ["numpad8", {lift: 1}, "Decorating: raise (2nd)"], ["numpad2", {lift: -1}, "Decorating: lower (2nd)"],
    ["delete", {sell: true}, "Decorating: sell the piece back"], ["numpad0", {cancel: true}, "Decorating: put back or finish"],
]) {
    Settings.describeKey(key, label);
    Key.bind(key, () => send(request));
}

Events.on("missionUnload", () => {
    decorating = false;
});
