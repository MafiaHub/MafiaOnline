// Scripted NPCs (Npc.spawn): the barman, the moonshiner, a policeman walking
// up for his envelope, the people a job puts in the street (lib/civilians.ts).
// The simulation host places an NPC a moment after the spawn (npcSpawn); only
// then can it move, talk or play a clip. The host may retire it again, or it
// may be killed, and the mission change removes every one. A Character
// follows one NPC through all of that; walkOff sends one away for good.
import { events, onMissionChange, onStop } from './lifecycle';
import { toast } from './notify';
import type { Point } from './util';

export type SpawnOptions = NonNullable<Parameters<typeof Npc.spawn>[3]>;

/** What ended a character's NPC: killed (by whom, if anyone) or retired by the host. */
export type Loss = { died: true; killer: Player | Npc | Vehicle | null } | { died: false };

export interface CharacterHooks {
    /** The host placed it: it can move, talk and play clips now. */
    onReady?(npc: Npc): void;
    /** It was killed or retired; the character is already empty. */
    onLost?(npc: Npc, loss: Loss): void;
}

/** Whether this server can spawn scripted NPCs at all. */
export function npcsAvailable(): boolean {
    return typeof Npc !== 'undefined' && typeof Npc.spawn === 'function';
}

// npc id -> the character it plays
const cast = new Map<number, Character>();

/** One scripted NPC a module keeps, spawned again as it needs. */
export class Character {
    npc: Npc | null = null;
    /** The host has placed it. */
    ready = false;
    /** A Date.now() time before which the module spawns no new one. */
    respawnAt = 0;

    constructor(private readonly hooks: CharacterHooks = {}) {}

    /** Spawns the NPC, replacing nothing: call it on an empty character. Null when refused. */
    spawn(model: string, position: Point, heading: number, options: SpawnOptions): Npc | null {
        this.npc = Npc.spawn(model, position, heading, options) ?? null;
        this.ready = false;

        if (this.npc) {
            cast.set(this.npc.id, this);
        }

        return this.npc;
    }

    /** The NPC once the host has placed it. */
    placed(): Npc | null {
        return this.ready ? this.npc : null;
    }

    /** The NPC once placed and while alive: one that can talk and move. */
    present(): Npc | null {
        const npc = this.placed();

        return npc?.alive ? npc : null;
    }

    /** A retail voice line (Npc.say), if it stands there to say it. */
    say(line: number): void {
        this.present()?.say(line);
    }

    remove(): void {
        this.npc?.remove();
        this.forget();
    }

    /** Lets go of the NPC without removing it (the mission change already did). */
    forget(): void {
        if (this.npc) {
            cast.delete(this.npc.id);
        }

        this.npc = null;
        this.ready = false;
    }

    /**
     * Sends the NPC walking straight to `point` and removes it after
     * `removeAfterMs` if it is still alive then: a character leaving the
     * scene. The character is empty at once; what happens to the NPC on its
     * way out is nobody's business, and the resource's stop leaves it to the
     * mission (walkOff's aliveOnly and keepOnStop).
     */
    walkOff(point: Point, removeAfterMs: number): void {
        const npc = this.npc;

        this.forget();

        if (npc) {
            walkOff(npc, removeAfterMs, {
                to: point,
                direct: true,
                aliveOnly: true,
                keepOnStop: true,
            });
        }
    }

    /** @internal */
    placedByHost(npc: Npc): void {
        this.ready = true;
        this.hooks.onReady?.(npc);
    }

    /** @internal */
    lostByHost(npc: Npc, loss: Loss): void {
        this.forget();
        this.hooks.onLost?.(npc, loss);
    }
}

events.on('npcSpawn', (npc) => cast.get(npc.id)?.placedByHost(npc));
events.on('npcDeath', (npc, killer) => cast.get(npc.id)?.lostByHost(npc, { died: true, killer }));
events.on('npcRetire', (npc) => cast.get(npc.id)?.lostByHost(npc, { died: false }));

// The mission change removed every NPC; a later one may reuse an id.
events.on('missionChange', () => {
    for (const character of [...cast.values()]) {
        character.forget();
    }
});

export interface WalkOffOptions {
    /** Where it walks; it stays where it is when omitted. */
    to?: Point | null;
    /** Straight there rather than along the walkways. */
    direct?: boolean;
    /** Runs when its time is up, before the removal. */
    gone?: () => void;
    /**
     * Only a living NPC walks off, and it is removed only if still alive when
     * its time is up; without it, a dead one is removed as well.
     */
    aliveOnly?: boolean;
    /** The resource's stop leaves it standing (the mission change removes it) rather than removing it at once. */
    keepOnStop?: boolean;
}

// NPCs sent off, removed when their time is up. A mission change has
// removed them already; the resource stopping removes them at once.
const strays = new Set<Npc>();

/** Walks off (to `to`, when given) and is removed `ms` later. */
export function walkOff(
    npc: Npc,
    ms: number,
    { to = null, direct = false, gone, aliveOnly = false, keepOnStop = false }: WalkOffOptions = {},
): void {
    if (aliveOnly && !npc.alive) {
        return;
    }

    if (to) {
        if (direct) {
            npc.walkTo(to, { direct: true });
        } else {
            npc.walkTo(to);
        }
    }

    if (!keepOnStop) {
        strays.add(npc);
    }

    setTimeout(() => {
        strays.delete(npc);
        gone?.();

        if (!aliveOnly || npc.alive) {
            npc.remove();
        }
    }, ms);
}

events.on('npcRetire', (npc) => {
    for (const stray of strays) {
        if (stray.id === npc.id) {
            strays.delete(stray);
        }
    }
});

onMissionChange(() => strays.clear());

onStop(() => {
    for (const npc of strays) {
        npc.remove();
    }

    strays.clear();
});

export interface SpeechOptions {
    /** Who says it in the world; a dead or missing one stays silent. */
    npc?: Npc | null;
    /** The retail voice line (Npc.say) it says. */
    voice?: number;
    seconds?: number;
}

/** A line of dialogue: the text on the player's screen, the voice in the world. */
export function speak(
    player: Player,
    who: string,
    line: string,
    { npc = null, voice, seconds = 8 }: SpeechOptions = {},
): void {
    toast(player, `"${line}"`, { title: who, tone: 'gold', seconds });

    if (npc?.alive && voice !== undefined) {
        npc.say(voice);
    }
}
