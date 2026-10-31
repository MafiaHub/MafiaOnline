// Yellow Pete himself: a scripted NPC (Npc.spawn) in his cellar while anyone
// is near his door, gone 30 seconds after the last of them leaves, as a
// bar's people are (bars/staff.ts). server/shops.ts sells across his counter
// and tells him when to greet, deal or refuse.
//
// Where he stands, and the way down to him: server/gunshop.ts.
//
// His clips are standing ones: Pete's own (PeteStativ and the Pete*On/Off
// hand-overs) lean on mission 19's counter, which is not here. He stands
// (BorecStat), folds his arms a while (GestikulaceNew03: the hands meet in
// front of the chest from frame 43 of 174), beckons whoever comes down
// (GestoPojdSem01: the right hand rises to 1.56 m and waves) and talks with
// his hands over a deal (GestikulaceNew01, the bar's talkers' clip).
import * as maps from './lib/maps';
import { Character, npcsAvailable, speak } from './lib/npcs';
import { events, every, onStop } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { distance, flatDistance, MAIN_WORLD, onStreet, pick } from './lib/util';
import { SPOT } from './gunshop';

const MODEL = 'ProdZ1.i3d';
// The old man's voice the moonshiner uses (distillery/characters.ts).
const VOICE = 27;
const NAME = 'Yellow Pete';

// He is about while anyone is this near: the street door is 23 m off.
const PRESENT_RANGE = 40;
const STAND_DOWN_MS = 30_000;
const RESPAWN_MS = 60_000;
const TICK_MS = 1000;
// Walked off (scared, shoved): he goes back past this.
const STRAY = 1.0;
// He greets someone this near, at most this often each.
const GREET_RANGE = 4.5;
const GREET_EVERY_MS = 3 * 60_000;
// How long he stays turned to a customer.
const ANSWER_MS = 5000;

type Step = readonly [clip: string, ms: number, loop: boolean];

const ROUTINE: readonly Step[] = [
    ['BorecStat.i3d', 9000, true],
    ['GestikulaceNew03.i3d', 7000, false],
    ['BorecStat.i3d', 12_000, true],
];
const BECKON = 'GestoPojdSem01.i3d';
const TALK = 'GestikulaceNew01.i3d';

// Retail voice lines (Npc.say): 6-8 a group event, the lines the distillery's
// people chat with.
const HELLO_VOICES: readonly number[] = [6, 8];
const DEAL_VOICE = 7;

const GREETINGS: readonly string[] = [
    'Come down, come down. Mind the last step.',
    'What will it be? Everything on the wall is for sale.',
    'Close the door behind you. The damp gets in the barrels.',
    'Business or pleasure? Down here it is the same thing.',
];

let current: Step | null = null;
let step = 0;
let until = 0;
let answeringUntil = 0;
let emptySince = 0;
const greeted = new PlayerMap<number>({ clearOnMissionChange: true });

const pete = new Character({
    onReady: (npc) => {
        npc.face(SPOT.heading);
        current = null;
        step = 0;
        until = 0;
        answeringUntil = 0;
    },
    // Killed, he is gone a while; retired by the host, he can come back at once.
    onLost: (_npc, loss) => {
        pete.respawnAt = loss.died ? Date.now() + RESPAWN_MS : 0;
    },
});

function open(): boolean {
    return Boolean(maps.current()?.shops) && World.isReady();
}

function near(player: Player, range: number): boolean {
    return onStreet(player) && distance(player.position, SPOT.position) <= range;
}

/** Whether Pete stands at his spot to deal: always, when this server cannot spawn people. */
export function serving(): boolean {
    const npc = pete.present();

    return npcsAvailable()
        ? Boolean(npc && flatDistance(npc.position, SPOT.position) <= STRAY + 0.5)
        : true;
}

function play(npc: Npc, clip: string, loop: boolean): void {
    npc.playAnimation(clip, { loop });
}

// The routine moves on after each step's time; a looping clip dropped (a
// task, a shove) is given again.
function animate(npc: Npc, now: number): void {
    if (now < answeringUntil) {
        return;
    }

    if (answeringUntil) {
        answeringUntil = 0;
        until = 0;
        npc.face(SPOT.heading);
    }

    if (now >= until) {
        current = ROUTINE[step++ % ROUTINE.length];
        play(npc, current[0], current[2]);
        until = now + current[1];
    } else if (current?.[2] && npc.animation.toLowerCase() !== current[0].toLowerCase()) {
        play(npc, current[0], true);
    }
}

// Back to his spot when something moved him; standing there, the routine.
// True when he stands at his spot.
function keepPost(npc: Npc, now: number): boolean {
    if (flatDistance(npc.position, SPOT.position) > STRAY) {
        if (!npc.getTask()) {
            npc.walkTo(SPOT.position, { direct: true, radius: 0.4 });
            until = 0;
            answeringUntil = 0;
        }

        return false;
    }

    animate(npc, now);

    return true;
}

function turnTo(npc: Npc, player: Player, clip: string, now: number): void {
    npc.face(player.position);
    play(npc, clip, false);
    answeringUntil = now + ANSWER_MS;
}

function greet(npc: Npc, now: number): void {
    for (const player of World.getPlayers()) {
        if (!near(player, GREET_RANGE) || now - (greeted.get(player.id) ?? 0) < GREET_EVERY_MS) {
            continue;
        }

        greeted.set(player.id, now);
        turnTo(npc, player, BECKON, now);
        speak(player, NAME, pick(GREETINGS), { npc, voice: pick(HELLO_VOICES), seconds: 6 });

        return;
    }
}

function tick(): void {
    if (!npcsAvailable()) {
        return;
    }

    const now = Date.now();

    if (!open()) {
        pete.remove();

        return;
    }

    if (World.getPlayers().some((player) => near(player, PRESENT_RANGE))) {
        emptySince = 0;

        if (!pete.npc && now >= pete.respawnAt) {
            const npc = pete.spawn(MODEL, SPOT.position, SPOT.heading, {
                behaviour: 'neutral',
                voice: VOICE,
                virtualWorld: MAIN_WORLD,
            });

            if (!npc) {
                pete.respawnAt = now + RESPAWN_MS;
            }
        }
    } else if (pete.npc) {
        emptySince ||= now;

        if (now - emptySince >= STAND_DOWN_MS) {
            pete.remove();
            emptySince = 0;
        }
    }

    const npc = pete.present();

    // A clip would stop him on his way back: he greets from his spot.
    if (npc && keepPost(npc, now) && now >= answeringUntil) {
        greet(npc, now);
    }
}

/**
 * Pete answers a customer: turns to them and says the line (on their
 * screen; in the world too, with `voice`). With `deal` he talks it over
 * with his hands.
 */
export function says(
    player: Player,
    line: string,
    { voice = false, deal = false }: { voice?: boolean; deal?: boolean } = {},
): void {
    const npc = pete.present();
    const now = Date.now();

    greeted.set(player.id, now);

    if (npc && deal) {
        turnTo(npc, player, TALK, now);
    } else if (npc) {
        npc.face(player.position);
        answeringUntil = now + ANSWER_MS;
    }

    speak(player, NAME, line, { npc, voice: voice ? DEAL_VOICE : undefined, seconds: 6 });
}

every(TICK_MS, tick);

// The mission change removed every NPC (lib/npcs.ts lets go of him).
events.on('missionChange', () => {
    pete.respawnAt = 0;
    emptySince = 0;
    answeringUntil = 0;
});

onStop(() => pete.remove());
