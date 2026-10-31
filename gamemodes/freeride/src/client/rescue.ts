// The job key of the fire brigade and the ambulance (server/lib/jobkey.ts):
// G signs on or off under a station's arrow, held it sprays the hose, leads a
// trapped civilian out or treats a patient; a downed player presses it to
// stop waiting for a medic. The server decides
// what a press does and sends the prompt and the treatment progress.
import { clamp } from './lib/geometry';
import { drawPrompt, keyHint } from './lib/panels';
import { events, onServer } from './lib/runtime';

const JOB_KEY = 'g';
const PING_MS = 250;
const BAR_FILL = 0xff8fc27a;
const BAR_TRACK = 0x80303a40;

interface RescuePrompt {
    text: string | null;
    /** The treatment so far, 0 to 1; null shows no bar. */
    progress: number | null;
}

let text: string | null = null;
let progress: number | null = null;
let holding = false;
let pingAt = 0;

Settings.describeKey(JOB_KEY, 'Job key: sign on, use, treat, plant, pick up');

Key.bind(JOB_KEY, 'both', (_key, state) => {
    holding = state === 'down';
    pingAt = Date.now();
    events.emitServer('fr:rescue:use', { down: holding });
});

onServer<RescuePrompt>('fr:rescue', (data) => {
    text = typeof data.text === 'string' ? data.text : null;
    progress = typeof data.progress === 'number' ? clamp(data.progress, 0, 1) : null;
});

events.on('render', () => {
    if (holding && Date.now() - pingAt >= PING_MS) {
        pingAt = Date.now();

        if (Key.isDown(JOB_KEY)) {
            events.emitServer('fr:rescue:use', { down: true });
        } else {
            holding = false;
            events.emitServer('fr:rescue:use', { down: false });
        }
    }

    if (!text) {
        return;
    }

    // The server words its prompts for G; show the key the player moved it to.
    const { center, top } = drawPrompt(text.replace('[G]', keyHint(JOB_KEY)), {
        rise: 140,
        width: 460,
        height: progress === null ? 36 : 50,
        accent: true,
    });

    if (progress !== null) {
        Draw.rect(center - 200, top + 36, 400, 6, BAR_TRACK);
        Draw.rect(center - 200, top + 36, Math.round(400 * progress), 6, BAR_FILL);
    }
});

events.on('missionUnload', () => {
    text = null;
    progress = null;
    holding = false;
});
