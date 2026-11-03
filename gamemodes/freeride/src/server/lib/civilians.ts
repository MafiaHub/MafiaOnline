// The city's people a job puts in the street: patients, people caught in a
// fire, passers-by. Their models, the clips and retail voice lines they use,
// and spawning one; once a job is done with them they walk off (lib/npcs.ts
// walkOff) like any other NPC.
import { npcsAvailable } from './npcs';
import { pick, type Point } from './util';

export const CIVILIANS: readonly string[] = [
    'Delnik01.i3d',
    'Delnik02.i3d',
    'Czena01.i3d',
    'Czena02.i3d',
    'Homeless01.i3d',
];
export const WOMEN: readonly string[] = ['Czena01.i3d', 'Czena02.i3d'];
export const MEN: readonly string[] = ['Delnik01.i3d', 'Delnik02.i3d', 'Homeless01.i3d'];

// Stock clips (anims\ in a4.dta) the retail panicking walkers use
// (C_human anim_info 337, 331): lying on the ground, crouched.
export const LYING = 'x panika leh stativ.i3d';
export const CROUCHED = 'x panika drep stativ.i3d';

// Retail voice lines (Npc.say): 21-23 finding someone (relief), 66-69 panic,
// 75-78 giving up, 85-91 pain.
export const RELIEF_LINES: readonly number[] = [21, 22, 23];
export const PANIC_LINES: readonly number[] = [66, 67, 68, 69];
export const GIVE_UP_LINES: readonly number[] = [75, 76, 77, 78];
export const PAIN_LINES: readonly number[] = [85, 86, 87, 88, 89, 90, 91];

/** The retail voices of men (26-28) and women (29-31), one per person. */
export function voiceFor(model: string): number {
    return (/^czena/i.test(model) ? 29 : 26) + Math.floor(Math.random() * 3);
}

export interface CivilianOptions {
    /** One of voiceFor's; a fresh one for the model when omitted. */
    voice?: number;
    /** An interior built apart from the street; the street world when omitted. */
    world?: number;
}

/** A neutral, healthy person in their own voice. */
export function spawnCivilian(
    model: string,
    position: Point,
    heading: number,
    { voice = voiceFor(model), world }: CivilianOptions = {},
): Npc | null {
    if (!npcsAvailable()) {
        return null;
    }

    return Npc.spawn(model, position, heading, {
        behaviour: 'neutral',
        health: 100,
        voice,
        ...(world === undefined ? {} : { virtualWorld: world }),
    });
}

/** One of the lines, in the person's own voice. */
export function say(npc: Npc, lines: readonly number[]): void {
    npc.say(pick(lines));
}
