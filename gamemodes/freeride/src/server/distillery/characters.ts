// Hank, the old moonshiner who runs the still, and the voices of everyone
// at the camp. He stands by the pot, smoking, while anyone who knows of the
// camp is within HANK_RANGE.
import { Character, npcsAvailable, speak } from '../lib/npcs';
import { flatDistance, headingFrom, MAIN_WORLD, onStreet } from '../lib/util';
import { QUEST, stageOf } from './operations';
import { HANK_RANGE, HANK_SPOT, POT } from './places';

// Retail lines (Npc.say): 6-8 a group event, 33-36 ready to fight, 45-50 a
// fight spotted, 66-69 panic, 75-78 giving up, 132-134 a car steal
// refused, 139-144 police halt.
export const LINE = {
    chat: 6,
    nod: 7,
    fight: 33,
    spotted: 45,
    panic: 66,
    shrug: 75,
    robbed: 132,
    halt: 139,
} as const;
export const SMOKING = 'KoureniAutoStativ.i3d';

// Retail voice ids (Npc.spawn).
const HANK = { model: 'Homeless01.i3d', voice: 27 };
const IDLE_MS = 30_000;
const RETRY_MS = 30_000;
// Hank went to fetch a doctor.
const LOST_MS = 60_000;

export const hank = new Character({
    onReady: (npc) => npc.playAnimation(SMOKING, { loop: true }),
    onLost: () => {
        hank.respawnAt = Date.now() + LOST_MS;
    },
});
let idleSince = 0;

function wanted(): boolean {
    return World.getPlayers().some(
        (player) =>
            onStreet(player) &&
            stageOf(player) >= QUEST.FIND &&
            flatDistance(player.position, POT) <= HANK_RANGE,
    );
}

export function tickHank(now: number): void {
    if (!npcsAvailable()) {
        return;
    }

    if (wanted()) {
        idleSince = 0;

        if (!hank.npc && now >= hank.respawnAt) {
            const npc = hank.spawn(HANK.model, HANK_SPOT, headingFrom(HANK_SPOT, POT), {
                behaviour: 'friendly',
                voice: HANK.voice,
                virtualWorld: MAIN_WORLD,
            });

            if (!npc) {
                hank.respawnAt = now + RETRY_MS;
            }
        }
    } else if (hank.npc) {
        idleSince ||= now;

        if (now - idleSince >= IDLE_MS) {
            hank.remove();
        }
    }
}

export function hankSays(
    player: Player,
    line: string,
    voice: number = LINE.chat,
    seconds = 8,
): void {
    speak(player, 'Hank', line, { npc: hank.placed(), voice, seconds });
}
