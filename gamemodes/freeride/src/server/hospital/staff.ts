// The people of the hospital: scripted NPCs (Npc.spawn) in the wards' world
// while anyone is inside, gone 20 seconds after the last visitor, as a
// bar's people are (bars/staff.ts). Mafia has no doctor or nurse, so they
// are stock people dressed for it: Barman01's long white apron makes the
// doctors, the maid Serv01 (dark dress, white apron) the nurses, civil38 in
// shirtsleeves the orderly, Delnik03 in a work coat the janitor and
// Czena03, in a navy suit with a white collar, the receptionist.
//
// Their clips are the story missions' own (anims\ in a4.dta and ac.dta),
// chosen by the pose they hold, read from the clips' bone tracks, and each
// loops back onto its first frame:
//  RecepcniStativ, "recepcni stativ 1/2/ukazuje"   the hotel receptionists
//      standing at their desk, the last pointing the way
//  PisLoop          sitting at a table writing, hands 0.9 m up 0.4 m ahead:
//                   the doctor at his desk
//  prohlidka        standing, bending to look at something 0.65 m ahead and
//                   0.3-0.6 m up: the surgeon over the theatre's bed
//  "x barman umyvani"  washing at counter height: the orderly at a basin
//  Zametani         sweeping: the janitor
//  "stativ uklizecka", "Mise06c Slecna stativ"   a woman standing: the nurses
//  "Mise zidle01st", ZidleSed03, SezCteni   sitting on a chair (the bar's
//                   drinkers), the last reading in the lap: the visitors
//  SamZrLehS        Sam lying wounded on his back, turned a little: pelvis
//                   0.13 m over the root, head 0.6 m behind, feet 0.7 ahead
// A clip is given again only when the NPC dropped it (Npc.animation).
//
// The job key (G) talks to whoever the player walks up to: they turn to the
// player and answer. The doctor patches a hurt player up for a fee, and the
// receptionist keeps the Dispensary's counter (server/shops.ts).
import * as room from '../lib/hospital_room';
import { Interactions } from '../lib/interactions';
import { Character, npcsAvailable, speak } from '../lib/npcs';
import { toast } from '../lib/notify';
import { PlayerSet } from '../lib/players';
import { distance, formatMoney, type Placement } from '../lib/util';

const STAND_DOWN_MS = 20_000;
const RESPAWN_MS = 60_000;
const TALK_REACH = 2.4;
// How long someone stays turned to the player after answering.
const ANSWER_MS = 5000;

/** The doctor heals a hurt player to full for this. */
export const DOCTOR_FEE = 75;

type Routine = readonly (readonly [clip: string, ms: number])[];

interface Role {
    name: string;
    model: string;
    spot: room.Spot;
    routine: Routine;
    /** What they answer when talked to. */
    lines: readonly string[];
    /** A retail voice line said with it (Npc.say), when they have one that fits. */
    voices?: readonly number[];
    /** Their prompt's verb: talk to a person, look in on a patient. */
    verb?: string;
    /** The doctor: patches a hurt player up. */
    heals?: boolean;
}

// Retail pain lines (85-91): a patient groans.
const GROANS: readonly number[] = [85, 86, 87, 88, 89];
const LYING: Routine = [['SamZrLehS.i3d', 0]];
const PATIENT_LINES: readonly string[] = [
    'Water... is there any water?',
    'They took the bullet out this morning. Feels like they left the gun in.',
    'Tell the doctor I can walk. Please.',
    "Don't let them see me in here.",
];
const NURSE_LINES: readonly string[] = [
    'Shh, they are sleeping. The doctor is in his room off the corridor.',
    'Bandages are at the front desk, if you are only scratched.',
    'Wipe your feet, please. We just did the floors.',
];

const { SPOTS } = room;

const ROLES: readonly Role[] = [
    {
        name: 'Receptionist',
        model: 'Czena03.i3d',
        spot: SPOTS.receptionist,
        routine: [
            ['RecepcniStativ.i3d', 9000],
            ['recepcni stativ 1.i3d', 6000],
            ['RecepcniStativ.i3d', 9000],
            ['recepcni stativ ukazuje.i3d', 7400],
            ['recepcni stativ 2.i3d', 7000],
        ],
        lines: [
            'Lost Heaven Hospital. Bandages and tincture are right here at the desk.',
            'The doctor sees people in his room down the corridor, on the left.',
            'Visiting hours are over, but nobody here turns away a man on his feet.',
            'If the police ask, the register says you were never here.',
        ],
    },
    {
        name: 'Doctor',
        model: 'Barman01.i3d',
        spot: SPOTS.doctor,
        routine: [['PisLoop.i3d', 0]],
        heals: true,
        lines: [
            'You look fit enough to me. Come back when you are bleeding.',
            'Two bullets this week and a car crash. This city keeps us busy.',
        ],
    },
    {
        name: 'Surgeon',
        model: 'Barman01.i3d',
        spot: SPOTS.surgeon,
        routine: [['prohlidka.i3d', 0]],
        lines: ['Not now. Out of my theatre, please.', 'Steady hands, quiet room. Out you go.'],
    },
    {
        name: 'Nurse',
        model: 'Serv01.i3d',
        spot: SPOTS.theatreNurse,
        routine: [['Mise06c Slecna stativ.i3d', 0]],
        lines: ['You cannot be in here. Wait in the corridor, please.'],
    },
    {
        name: 'Orderly',
        model: 'civil38.i3d',
        spot: SPOTS.orderly,
        routine: [['x barman umyvani.i3d', 0]],
        lines: ['Scrubbing up for the next one. They never stop coming.', 'Mind the bucket.'],
    },
    {
        name: 'Nurse',
        model: 'Serv01.i3d',
        spot: SPOTS.menNurse,
        routine: [['stativ uklizecka.i3d', 0]],
        lines: NURSE_LINES,
    },
    {
        name: 'Nurse',
        model: 'Serv01.i3d',
        spot: SPOTS.womenNurse,
        routine: [['Mise06c Slecna stativ.i3d', 0]],
        lines: NURSE_LINES,
    },
    {
        name: 'Janitor',
        model: 'Delnik03.i3d',
        spot: SPOTS.janitor,
        routine: [['Zametani.i3d', 0]],
        lines: [
            'Careful, the floor is wet.',
            "Forty years of floors. This one's the worst. Blood never comes out of pine.",
        ],
    },
    {
        name: 'Visitor',
        model: 'civil14.i3d',
        spot: SPOTS.waiting[0],
        routine: [['Mise zidle01st.i3d', 0]],
        lines: ['Three hours I have been waiting. Three hours.'],
    },
    {
        name: 'Visitor',
        model: 'Czena05.i3d',
        spot: SPOTS.waiting[1],
        routine: [['ZidleSed03.i3d', 0]],
        lines: ["My husband is in there. They won't tell me anything."],
    },
    {
        name: 'Visitor',
        model: 'civil13.i3d',
        spot: SPOTS.waiting[2],
        routine: [['SezCteni.i3d', 0]],
        lines: ['Says here the Salieri boys own half the docks now. Imagine that.'],
    },
    {
        name: 'Visitor',
        model: 'Czena01.i3d',
        spot: SPOTS.bedside,
        routine: [['ZidleSed03.i3d', 0]],
        lines: ['He fell off a truck at the docks. That is what he says, anyway.'],
    },
    ...[...SPOTS.men, SPOTS.onTable].map((spot, index): Role => ({
        name: 'Patient',
        model: ['civil33.i3d', 'civil36.i3d', 'Delnik01.i3d', 'civil39.i3d'][index],
        spot,
        routine: LYING,
        lines: PATIENT_LINES,
        voices: GROANS,
        verb: 'LOOK IN ON THE PATIENT',
    })),
    ...SPOTS.women.map((spot, index): Role => ({
        name: 'Patient',
        model: ['Czena08.i3d', 'Czena07.i3d'][index],
        spot,
        routine: LYING,
        lines: PATIENT_LINES,
        voices: GROANS,
        verb: 'LOOK IN ON THE PATIENT',
    })),
];

interface Member {
    role: Role;
    place: Placement;
    character: Character;
    /** The routine's next clip. */
    step: number;
    /** When the clip playing gives way to the next. */
    until: number;
    /** Turned to a player until then. */
    answeringUntil: number;
}

let members: Member[] = [];
let emptySince = 0;

function member(role: Role): Member {
    const entry: Member = {
        role,
        place: room.placed(role.spot),
        character: new Character({
            onReady: (npc) => {
                npc.face(entry.place.heading);
                entry.until = 0;
                animate(entry, Date.now());
            },
            onLost: () => {
                entry.character.respawnAt = Date.now() + RESPAWN_MS;
            },
        }),
        step: 0,
        until: 0,
        answeringUntil: 0,
    };

    return entry;
}

function cast(): Member[] {
    if (!members.length) {
        members = ROLES.map(member);
    }

    return members;
}

// A routine of several clips moves on to the next after its time; a single
// looping clip is given again only when the NPC dropped it. An NPC that
// turned to answer someone turns back first.
function animate(entry: Member, now: number): void {
    const npc = entry.character.placed();

    if (!npc || now < entry.answeringUntil) {
        return;
    }

    if (entry.answeringUntil) {
        entry.answeringUntil = 0;
        entry.until = 0;
        npc.face(entry.place.heading);
    }

    const routine = entry.role.routine;

    if (routine.length === 1) {
        const [clip] = routine[0];

        if (npc.animation.toLowerCase() !== clip.toLowerCase()) {
            npc.playAnimation(clip, { loop: true });
        }

        return;
    }

    if (now < entry.until) {
        return;
    }

    const [clip, ms] = routine[entry.step % routine.length];

    entry.step++;
    npc.playAnimation(clip, { loop: true });
    entry.until = now + ms;
}

function staffUp(now: number): void {
    if (!npcsAvailable() || !room.isBuilt()) {
        return;
    }

    for (const entry of cast()) {
        if (entry.character.npc || now < entry.character.respawnAt) {
            continue;
        }

        const { position, heading } = entry.place;

        entry.step = 0;
        entry.until = 0;
        entry.answeringUntil = 0;

        if (
            !entry.character.spawn(entry.role.model, position, heading, {
                behaviour: 'neutral',
                virtualWorld: room.WORLD,
            })
        ) {
            entry.character.respawnAt = now + RESPAWN_MS;
        }
    }
}

/** Everyone leaves (the resource stops, nobody is inside). */
export function standDown(): void {
    for (const entry of members) {
        entry.character.remove();
        entry.character.respawnAt = 0;
    }

    emptySince = 0;
}

/** The mission change removed the NPCs (lib/npcs.ts lets go of them). */
export function forget(): void {
    members = [];
    emptySince = 0;
}

function inside(player: Player): boolean {
    return player.virtualWorld === room.WORLD && room.inRoom(player.position, 1);
}

/** Staffs the hospital while someone is inside; stands them down once empty a while. */
export function tickStaff(now: number): void {
    const visitors = World.getPlayers().filter(inside);

    if (visitors.length) {
        emptySince = 0;
        staffUp(now);
    } else if (members.some((entry) => entry.character.npc)) {
        if (!emptySince) {
            emptySince = now;
        } else if (now - emptySince >= STAND_DOWN_MS) {
            standDown();
        }
    }

    for (const entry of members) {
        animate(entry, now);
    }
}

/* Talking to them (G) */

type Action = { kind: 'talk'; entry: Member } | { kind: 'heal'; entry: Member };

function nearest(player: Player): Member | null {
    let best: { entry: Member; gap: number } | null = null;

    for (const entry of members) {
        const npc = entry.character.present();
        const gap = npc ? distance(player.position, npc.position) : Infinity;

        if (gap <= TALK_REACH && (!best || gap < best.gap)) {
            best = { entry, gap };
        }
    }

    return best?.entry ?? null;
}

function hurt(player: Player): boolean {
    return player.health < 100;
}

function pick<T>(list: readonly T[]): T {
    return list[Math.floor(Math.random() * list.length)];
}

// They stop what they are doing and turn to the player for a moment.
function turnTo(entry: Member, player: Player): Npc | null {
    const npc = entry.character.present();

    if (npc) {
        npc.face(player.position);
        entry.answeringUntil = Date.now() + ANSWER_MS;
    }

    return npc;
}

function talk(player: Player, entry: Member): void {
    const npc = turnTo(entry, player);
    const voices = entry.role.voices;

    speak(player, entry.role.name, pick(entry.role.lines), {
        npc,
        voice: voices ? pick(voices) : undefined,
    });
}

function heal(player: Player, entry: Member): void {
    const npc = turnTo(entry, player);

    if (!hurt(player)) {
        talk(player, entry);

        return;
    }

    if (!player.trySpendMoney(DOCTOR_FEE)) {
        speak(
            player,
            entry.role.name,
            `It is ${formatMoney(DOCTOR_FEE)} to stitch you up, and you have ${formatMoney(player.money)}. The desk sells bandages.`,
            { npc },
        );

        return;
    }

    if (!player.setHealth(100)) {
        player.giveMoney(DOCTOR_FEE);
        toast(player, 'The doctor could not see you; your money was returned.', { tone: 'bad' });

        return;
    }

    speak(player, entry.role.name, 'Sit still. There. Clean, stitched and good as new.', {
        npc,
    });

    toast(player, `Patched up for ${formatMoney(DOCTOR_FEE)}: health 100/100.`, { tone: 'good' });
}

// The prompt shares the side activities' layer with the distillery: only
// players inside are given hints, and one who leaves has theirs taken down.
const prompted = new PlayerSet({ clearOnMissionChange: true });

const interactions = new Interactions<Action>({
    layer: 'side',
    enabled: () => members.length > 0,
    busy: (player) => !inside(player),
    choose: (player) => {
        if (!inside(player) || player.getVehicle()) {
            return null;
        }

        const entry = nearest(player);

        if (!entry) {
            return null;
        }

        return entry.role.heals && hurt(player) ? { kind: 'heal', entry } : { kind: 'talk', entry };
    },
    hint: (_player, action) =>
        action.kind === 'heal'
            ? `[G] PATCH ME UP · ${formatMoney(DOCTOR_FEE)}`
            : `[G] ${action.entry.role.verb ?? `TALK TO THE ${action.entry.role.name.toUpperCase()}`}`,
    press: (player, action) => {
        if (action.kind === 'heal') {
            heal(player, action.entry);
        } else {
            talk(player, action.entry);
        }
    },
    holds: {},
    finish: () => {},
});

/** The job key's prompt for the players inside; taken down as they leave. */
export function showHints(): void {
    for (const player of World.getPlayers()) {
        if (inside(player)) {
            prompted.add(player.id);
        } else if (prompted.delete(player.id)) {
            interactions.prompt(player, null);
        }
    }

    interactions.showHints();
}

/** A fresh life shows no prompt. */
export function forgetHint(player: Player): void {
    prompted.delete(player.id);
    interactions.forget(player);
}
