// The job key (G, client/rescue.ts): sign on or off under a station's arrow
// (lib/stations.ts), spray the hose, lead a trapped civilian out, treat a
// patient, give up waiting for a medic. The client reports the key down,
// keeps pinging while it is held and reports it up; a held key nobody pinged
// for a while counts as released, so a chat window that swallowed the key-up
// cannot leave a hose running. Modules register in order; the first that
// takes a press owns it until the key goes up.
import { onClient, emit, playerById } from './util';
import { events, every } from './lifecycle';
import { fields } from './payload';
import { PlayerMap } from './players';

const PING_TIMEOUT_MS = 800;

export interface JobKeyHandler {
    /** True takes the press: this handler owns it until the key goes up. */
    press(player: Player): boolean;
    release?(player: Player): void;
}

interface Held {
    owner: JobKeyHandler | null;
    pingAt: number;
}

const handlers: JobKeyHandler[] = [];
const held = new PlayerMap<Held>();

export function register(handler: JobKeyHandler): void {
    handlers.push(handler);
}

function releaseId(playerId: number): void {
    const entry = held.get(playerId);

    if (!entry) {
        return;
    }

    held.delete(playerId);
    const player = playerById(playerId);

    if (player) {
        entry.owner?.release?.(player);
    }
}

export function release(player: Player): void {
    releaseId(player.id);
}

export function isHeld(player: Player): boolean {
    return held.has(player.id);
}

// Who may hold the prompt line, strongest first: standing at a station's
// sign-on point beats a job's own hint, which beats a home's door (housing),
// which beats a side activity's (the rum distillery's) hint.
const PROMPT_LAYERS = ['station', 'job', 'home', 'side'] as const;

export type PromptLayer = (typeof PROMPT_LAYERS)[number];

interface Prompt {
    text: string;
    progress: number | null;
}

const prompts = new PlayerMap<Partial<Record<PromptLayer, Prompt | null>>>();

/** The prompt line and an optional progress bar (0-1) above it; null clears this layer and shows the next one down. */
export function prompt(
    player: Player,
    text: string | null,
    progress: number | null = null,
    layer: PromptLayer = 'job',
): void {
    const layers = prompts.get(player.id) ?? {};

    layers[layer] = text ? { text, progress } : null;
    prompts.set(player.id, layers);
    const top = PROMPT_LAYERS.map((name) => layers[name]).find(Boolean) ?? null;

    emit(player, 'fr:rescue', { text: top?.text ?? null, progress: top?.progress ?? null });
}

onClient('fr:rescue:use', (player, data) => {
    const entry = held.get(player.id);
    const down = Boolean(fields(data).down);

    if (!down) {
        releaseId(player.id);

        return;
    }

    if (entry) {
        entry.pingAt = Date.now();

        return;
    }

    const owner = handlers.find((handler) => handler.press(player)) ?? null;

    held.set(player.id, { owner, pingAt: Date.now() });
});

every(250, () => {
    const now = Date.now();

    for (const [id, entry] of [...held]) {
        if (now - entry.pingAt > PING_TIMEOUT_MS) {
            releaseId(id);
        }
    }
});

events.on('playerDeath', (player) => releaseId(player.id));
