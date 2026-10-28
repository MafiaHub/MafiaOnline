// The job key of the fire brigade and the ambulance (server/lib/jobkey.js):
// G held sprays the hose, leads a trapped civilian out or treats a patient;
// a downed player presses it to stop waiting for a medic. The server decides
// what a press does and sends the prompt and the treatment progress.
const JOB_KEY = "g";
const PING_MS = 250;
const PROMPT_COLOR = 0xffe7c47d;
const PROMPT_FILL = 0xd0121519;
const BAR_FILL = 0xff8fc27a;
const BAR_TRACK = 0x80303a40;

const view = {text: null, progress: null};
let holding = false;
let pingAt = 0;

Key.bind(JOB_KEY, "both", (_key, state) => {
    holding = state === "down";
    pingAt = Date.now();
    Events.emitServer("fr:rescue:use", {down: holding});
});

Events.on("fr:rescue", (data) => {
    view.text = typeof data?.text === "string" ? data.text : null;
    view.progress = typeof data?.progress === "number" ? Math.max(0, Math.min(1, data.progress)) : null;
});

Events.on("render", () => {
    if (holding && Date.now() - pingAt >= PING_MS) {
        pingAt = Date.now();
        if (Key.isDown(JOB_KEY)) Events.emitServer("fr:rescue:use", {down: true});
        else {
            holding = false;
            Events.emitServer("fr:rescue:use", {down: false});
        }
    }
    if (!view.text) return;
    const {width, height} = Web.getScreenSize();
    const top = height - 140;
    Draw.rect(width / 2 - 230, top, 460, view.progress === null ? 36 : 50, PROMPT_FILL);
    Draw.text(view.text, width / 2, top + 24, 15, PROMPT_COLOR, 3);
    if (view.progress !== null) {
        Draw.rect(width / 2 - 200, top + 36, 400, 6, BAR_TRACK);
        Draw.rect(width / 2 - 200, top + 36, Math.round(400 * view.progress), 6, BAR_FILL);
    }
});

Events.on("missionUnload", () => {
    view.text = null;
    view.progress = null;
    holding = false;
});
