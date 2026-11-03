// Calls for the officers on duty when no player is wanted: the streets are
// never quite quiet. Every few minutes the precinct radios one in, near an
// officer:
//  snatcher  a hood (Hoolig) has grabbed a woman's handbag and stands about
//            up the street; he runs once an officer comes near. G next to
//            him, or knocking him down (fists, a car), arrests him. Dead, he
//            pays little.
//  robbery   two armed gangsters (Gangster01-05) holding up a corner. They
//            open fire on the first officers on duty who come near, nobody
//            else, and pay for each one taken down.
// A call nobody answers goes cold after a few minutes. The NPCs are
// lib/npcs.ts Characters: the host placing them, killing or retiring them
// and the mission change are all followed, and the resource's stop removes
// them.
import * as jobkey from '../lib/jobkey';
import * as log from '../lib/log';
import * as serverConsole from '../lib/console';
import { CallClock, roadside, where } from '../lib/callouts';
import { CROUCHED, GIVE_UP_LINES, PANIC_LINES, WOMEN, say, voiceFor } from '../lib/civilians';
import { Character, npcsAvailable, walkOff, type Loss } from '../lib/npcs';
import { COLORS, feed, markers, name, text, toast, type Marker } from '../lib/notify';
import { events, onMissionChange, onStop } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';
import { reward } from '../lib/rewards';
import {
    MAIN_WORLD,
    distance,
    flatDistance,
    formatMoney,
    inFreeRide,
    onStreet,
    pick,
    playable,
    type Point,
} from '../lib/util';

export type PoliceCallKind = 'snatcher' | 'robbery';

const FIRST_CALL_MS = 60_000;
const CALL_EVERY_MS = 3.5 * 60_000;
const CALL_LIFETIME_MS = 5 * 60_000;
// How far from an officer a call comes in.
const MIN_DISTANCE = 300;
const MAX_DISTANCE = 900;
// The snatcher bolts when an officer on duty comes this close.
const BOLT_RANGE = 45;
const CATCH_REACH = 3;
// The robbers open fire on officers on duty this close.
const FIRE_RANGE = 40;
const SNATCHER_PAY = 160;
const SNATCHER_DEAD_PAY = 50;
const ROBBER_PAY = 150;
const XP = 50;
const MARKER_RANGE = 1200;
const SNATCHERS: readonly string[] = ['Hoolig01.i3d', 'Hoolig02.i3d', 'Hoolig03.i3d'];
const ROBBERS: readonly string[] = [
    'Gangster02.i3d',
    'Gangster03.i3d',
    'Gangster04.i3d',
    'Gangster05.i3d',
];
// The hood's voice, as the arsonist's (firefighters/arson.ts).
const HOOD_VOICE = 13;

type SuspectState = 'spawning' | 'loose' | 'caught' | 'dead' | 'gone';

interface Suspect {
    character: Character;
    state: SuspectState;
    armed: boolean;
    /** The running task (npcTaskComplete starts the next), or the officer under fire. */
    task: number;
    targetId: number | null;
    fleeing: boolean;
}

interface PoliceCall {
    kind: PoliceCallKind;
    position: Point;
    where: string;
    suspects: Suspect[];
    /** The woman who lost her bag, crouched on the pavement. */
    victim: Character | null;
    openedAt: number;
}

/** What the calls ask of police duty (index.ts). */
export interface CallRoster {
    onDuty(player: Player): boolean;
    /** Adds to what the officer earned this shift. */
    earn(player: Player, amount: number): void;
}

let roster: CallRoster | null = null;
let open: PoliceCall | null = null;
let lastKind: PoliceCallKind | null = null;
const clock = new CallClock(FIRST_CALL_MS);
// The grab prompt each officer was shown.
const prompts = new PlayerMap<string | null>({ clearOnMissionChange: true });

export function configure(next: CallRoster): void {
    roster = next;
}

function officers(): Player[] {
    return World.getPlayers().filter(
        (player) => roster?.onDuty(player) && onStreet(player) && player.alive,
    );
}

function loose(call: PoliceCall): Suspect[] {
    return call.suspects.filter(
        (suspect) => suspect.state === 'loose' || suspect.state === 'spawning',
    );
}

function spawnSuspect(call: PoliceCall, model: string, at: Point, armed: boolean): void {
    const suspect: Suspect = {
        character: new Character({
            onReady: () => {
                if (suspect.state === 'spawning') {
                    suspect.state = 'loose';
                }
            },
            onLost: (npc, loss) => lost(call, suspect, npc, loss),
        }),
        state: 'spawning',
        armed,
        task: 0,
        targetId: null,
        fleeing: false,
    };
    const npc = suspect.character.spawn(model, at, Math.random() * Math.PI * 2, {
        behaviour: 'neutral',
        health: armed ? 120 : 100,
        voice: HOOD_VOICE,
        virtualWorld: MAIN_WORLD,
        ...(armed ? { weapon: 6, ammo: 6, reserve: 30 } : {}),
    });

    if (npc) {
        call.suspects.push(suspect);
    }
}

function radio(call: PoliceCall, message: string): void {
    for (const officer of officers()) {
        toast(officer, message, { title: 'Police radio', tone: 'gold', seconds: 9 });
    }

    feed('police', [text('Police radio', COLORS.blue), text(` · ${call.where}`)]);
}

function startCall(kind: PoliceCallKind, origin: Point | null): boolean {
    const spot = roadside(origin, MIN_DISTANCE, MAX_DISTANCE, 4.5);
    const position = { x: spot.position.x, y: spot.position.y + 0.2, z: spot.position.z };
    const call: PoliceCall = {
        kind,
        position,
        where: where(position),
        suspects: [],
        victim: null,
        openedAt: Date.now(),
    };

    if (kind === 'snatcher') {
        // A few steps from her on the same pavement, as the arsonist stands
        // off his house (firefighters/arson.ts): further out could be inside
        // a building.
        const ahead = {
            x: position.x + Math.sin(spot.heading) * 3,
            y: position.y,
            z: position.z + Math.cos(spot.heading) * 3,
        };

        spawnSuspect(call, pick(SNATCHERS), ahead, false);
        const victim = new Character({
            onReady: (npc) => {
                npc.playAnimation(CROUCHED, { loop: true });
                say(npc, PANIC_LINES);
            },
        });
        const model = pick(WOMEN);

        victim.spawn(model, position, spot.heading, {
            behaviour: 'neutral',
            health: 100,
            voice: voiceFor(model),
            virtualWorld: MAIN_WORLD,
        });

        call.victim = victim;
    } else {
        for (const side of [-1, 1]) {
            spawnSuspect(
                call,
                pick(ROBBERS),
                {
                    x: position.x + Math.cos(spot.heading) * side * 1.5,
                    y: position.y,
                    z: position.z - Math.sin(spot.heading) * side * 1.5,
                },
                true,
            );
        }
    }

    if (!call.suspects.length) {
        call.victim?.remove();

        return false;
    }

    open = call;
    lastKind = kind;
    radio(
        call,
        kind === 'snatcher'
            ? `A woman had her handbag snatched ${call.where}. The thief is still on the street: he runs when he sees you. ` +
                  'Grab him with G or knock him down.'
            : `Armed robbery ${call.where}: two gunmen holding up the corner. They shoot at officers on sight. ` +
                  'Take them down, and call backup if you need it.',
    );

    return true;
}

// Away down the street at a run, again when he gets there.
function flee(suspect: Suspect): void {
    const npc = suspect.character.present();

    if (!npc) {
        return;
    }

    const away = roadside(npc.position, 100, 220, 4.5);

    suspect.fleeing = true;
    suspect.task = npc.walkTo(away.position, { run: true, radius: 3 });
}

function lost(call: PoliceCall, suspect: Suspect, npc: Npc, loss: Loss): void {
    if (suspect.state === 'caught' || suspect.state === 'gone') {
        return;
    }

    if (!loss.died) {
        suspect.state = 'gone';

        return;
    }

    suspect.state = 'dead';
    // The body stays a while, as the street calls' do.
    walkOff(npc, 30_000);
    const killer = loss.killer instanceof Vehicle ? loss.killer.getDriver() : loss.killer;

    if (!(killer instanceof Player) || !roster?.onDuty(killer)) {
        return;
    }

    const pay = suspect.armed ? ROBBER_PAY : SNATCHER_DEAD_PAY;

    roster.earn(killer, pay);
    reward(killer, {
        money: pay,
        counters: ['arrests'],
        xp: suspect.armed ? XP : 10,
        reason: suspect.armed ? 'Took down a gunman' : 'Stopped a thief',
        feed: {
            kind: 'police',
            parts: [
                text('Officer ', COLORS.blue),
                name(killer, COLORS.blue),
                text(
                    suspect.armed
                        ? ` took down a gunman ${call.where}`
                        : ` shot a thief ${call.where}`,
                ),
            ],
        },
    });

    toast(
        killer,
        suspect.armed
            ? `One gunman down. ${formatMoney(pay)}.`
            : `The thief is dead. ${formatMoney(pay)}: the city wanted him alive.`,
        { title: 'Police duty', tone: suspect.armed ? 'good' : 'gold' },
    );
}

function arrest(call: PoliceCall, suspect: Suspect, officer: Player): void {
    const npc = suspect.character.npc;

    suspect.state = 'caught';
    suspect.character.forget();

    if (npc) {
        npc.stop();
        npc.playAnimation(CROUCHED, { loop: true });
        say(npc, GIVE_UP_LINES);
        walkOff(npc, 15_000);
    }

    roster?.earn(officer, SNATCHER_PAY);
    reward(officer, {
        money: SNATCHER_PAY,
        counters: ['arrests'],
        xp: XP,
        reason: 'Caught a thief',
        feed: {
            kind: 'police',
            parts: [
                text('Officer ', COLORS.blue),
                name(officer, COLORS.blue),
                text(` caught a bag snatcher ${call.where}`),
            ],
        },
    });

    toast(officer, `Got him, and the bag. ${formatMoney(SNATCHER_PAY)} from the precinct.`, {
        title: 'Police duty',
        tone: 'good',
    });
}

function endCall(call: PoliceCall, message: string | null): void {
    open = null;

    for (const suspect of call.suspects) {
        if (suspect.state === 'loose' || suspect.state === 'spawning') {
            suspect.state = 'gone';
            suspect.character.remove();
        }
    }

    // She goes home.
    const victim = call.victim?.npc;

    call.victim?.forget();
    call.victim = null;

    if (victim) {
        victim.stopAnimation();
        walkOff(victim, 20_000, { aliveOnly: true });
    }

    if (message) {
        feed('police', [text('Police', COLORS.muted), text(` · ${message}`)]);
    }

    present();
}

function snatcherNear(player: Player): { call: PoliceCall; suspect: Suspect } | null {
    const call = open;

    if (!call || call.kind !== 'snatcher') {
        return null;
    }

    const suspect = call.suspects.find((entry) => {
        const npc = entry.character.present();

        return (
            entry.state === 'loose' &&
            npc !== null &&
            distance(npc.position, player.position) <= CATCH_REACH
        );
    });

    return suspect ? { call, suspect } : null;
}

// The snatcher bolts at the sight of an officer; the robbers fire on the nearest.
function work(call: PoliceCall, cops: Player[]): void {
    for (const suspect of call.suspects) {
        const npc = suspect.character.present();

        if (suspect.state !== 'loose' || !npc) {
            continue;
        }

        const nearest = cops
            .map((player) => ({ player, gap: distance(player.position, npc.position) }))
            .sort((a, b) => a.gap - b.gap)[0];

        if (!suspect.armed) {
            if (!suspect.fleeing && nearest && nearest.gap <= BOLT_RANGE) {
                say(npc, PANIC_LINES);
                flee(suspect);
            }

            continue;
        }

        if (nearest && nearest.gap <= FIRE_RANGE) {
            if (suspect.targetId !== nearest.player.id || !npc.getTask()) {
                suspect.targetId = nearest.player.id;
                npc.attack(nearest.player);
            }
        } else if (suspect.targetId !== null) {
            suspect.targetId = null;
            npc.stop();
        }
    }
}

function present(): void {
    const call = open;
    const items: Marker[] = [];

    if (call) {
        for (const [index, suspect] of call.suspects.entries()) {
            const npc = suspect.character.present();

            if (suspect.state === 'loose' && npc) {
                const { x, y, z } = npc.position;

                items.push({
                    id: `pc${index}`,
                    position: { x: Math.round(x), y: Math.round(y), z: Math.round(z) },
                    label: suspect.armed ? 'GUNMAN' : 'THIEF',
                    color: COLORS.red,
                    range: MARKER_RANGE,
                    near: 60,
                    map: {
                        icon: 'triangle',
                        label: suspect.armed ? 'Armed robbery' : 'Bag snatcher',
                    },
                });
            }
        }
    }

    for (const player of World.getPlayers()) {
        markers(player, 'police-call', roster?.onDuty(player) ? items : []);
    }
}

/** Each police duty tick: a new call now and then, the open one worked, ended or gone cold. */
export function update(now: number): void {
    if (!inFreeRide() || !World.isReady() || !roster) {
        return;
    }

    const cops = officers();
    const call = open;

    if (call) {
        work(call, cops);

        if (!loose(call).length) {
            const caught = call.suspects.some(
                (suspect) => suspect.state === 'caught' || suspect.state === 'dead',
            );

            endCall(call, caught ? null : `the suspects ${call.where} slipped away`);
        } else if (now - call.openedAt >= CALL_LIFETIME_MS) {
            endCall(call, `the call ${call.where} went cold`);
        } else {
            present();
        }

        return;
    }

    if (!cops.length || !npcsAvailable() || !clock.due(now, CALL_EVERY_MS)) {
        return;
    }

    const kinds: PoliceCallKind[] = ['snatcher', 'robbery'];
    const fresh = kinds.filter((kind) => kind !== lastKind);
    const anchor = pick(cops);

    startCall(pick(fresh.length ? fresh : kinds), anchor?.position ?? null);
}

/** For the officer's card and compass: the open call and how far it is. */
export function callFor(player: Player): { label: string; position: Point; gap: number } | null {
    const call = open;

    if (!call) {
        return null;
    }

    const target =
        loose(call)
            .flatMap((suspect) => {
                const npc = suspect.character.present();

                return npc ? [npc.position] : [];
            })
            .sort((a, b) => distance(a, player.position) - distance(b, player.position))[0] ??
        call.position;

    return {
        label:
            call.kind === 'snatcher'
                ? 'Bag snatcher'
                : `Armed robbery · ${loose(call).length} left`,
        position: target,
        gap: flatDistance(target, player.position),
    };
}

/** Off duty: the grab prompt goes (the shift clears only its own layer). */
export function clearPrompt(player: Player): void {
    if (prompts.get(player.id)) {
        jobkey.prompt(player, null, null, 'side');
    }

    prompts.delete(player.id);
}

/** The grab prompt for an officer standing next to the thief. */
export function promptOfficer(player: Player): void {
    const hint =
        roster?.onDuty(player) && playable(player) && !player.getVehicle() && snatcherNear(player)
            ? '[G] ARREST HIM'
            : null;

    if (hint !== (prompts.get(player.id) ?? null)) {
        prompts.set(player.id, hint);
        jobkey.prompt(player, hint, null, 'side');
    }
}

jobkey.register({
    press(player) {
        if (!roster?.onDuty(player) || !playable(player) || player.getVehicle()) {
            return false;
        }

        const near = snatcherNear(player);

        if (!near) {
            return false;
        }

        arrest(near.call, near.suspect, player);

        return true;
    },
});

// "fr police [snatcher|robbery]": a precinct call now, for trying them out.
serverConsole.action('fr', 'police', ([kind]) => {
    const chosen: PoliceCallKind = kind === 'robbery' ? 'robbery' : 'snatcher';

    if (open) {
        endCall(open, 'the precinct called off the last call');
    }

    const anchor = officers()[0] ?? World.getPlayers()[0];

    log.info(
        startCall(chosen, anchor?.position ?? null)
            ? `Started a ${chosen} call ${open?.where ?? ''}`
            : 'No police call could start (no NPCs on this server yet)',
    );
});

// He keeps running while he is loose.
events.on('npcTaskComplete', (npc, info) => {
    const suspect = open?.suspects.find((entry) => entry.character.npc?.id === npc.id);

    if (
        suspect &&
        !suspect.armed &&
        suspect.fleeing &&
        info.id === suspect.task &&
        info.result !== 'cancelled'
    ) {
        flee(suspect);
    }
});

// Knocked down by an officer on duty (a punch, a car), the thief gives up.
events.on('npcDamage', (npc, info) => {
    const call = open;

    if (!call || call.kind !== 'snatcher' || info.healthAfter <= 0) {
        return;
    }

    const suspect = call.suspects.find((entry) => entry.character.npc?.id === npc.id);
    const attacker = info.attacker instanceof Vehicle ? info.attacker.getDriver() : info.attacker;

    if (suspect?.state === 'loose' && attacker instanceof Player && roster?.onDuty(attacker)) {
        arrest(call, suspect, attacker);
    }
});

onMissionChange(() => {
    open = null;
    clock.after(FIRST_CALL_MS);
});

onStop(() => {
    const call = open;

    if (call) {
        for (const suspect of call.suspects) {
            suspect.character.remove();
        }

        call.victim?.remove();
        open = null;
    }

    for (const [id, hint] of prompts) {
        const player = World.getPlayers().find((entry) => entry.id === id);

        if (player && hint) {
            jobkey.prompt(player, null, null, 'side');
        }
    }

    prompts.clear();
});
