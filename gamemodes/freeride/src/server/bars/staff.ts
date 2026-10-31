// The people of a bar are scripted NPCs (Npc.spawn) in the bar's world while
// anyone is inside, and leave 20 seconds after the last guest. Their places,
// models and clips are the story missions' own: Luigi stands behind
// Salieri's counter (MISE02-ULICKA, MISE10-SALIERY), Salieri's men sit in
// the booths (MISE02-ULICKA: "Mise zidle01st", "ZidleSed03", "ZidleSed04"),
// a sailor waits in the billiard room (MISE16-SALIERY), and Morello's barman
// cleans up between Morello's men (MISE03-MORELLO: "x barman stativ", "x
// barman stul", "x barman umyvani"). The clips play with npc.playAnimation
// as the retail scripts' PERSON_PLAYANIM does.
import * as barRoom from '../lib/bar_room';
import { Character, npcsAvailable } from '../lib/npcs';
import { CHEERS, SERVING, type Bar, type StaffSpec } from './venues';

const STAND_DOWN_MS = 20_000;
const RESPAWN_MS = 60_000;
const SERVE_MS = 3000;
const CHEERS_MS = 4000;
// A lone looping clip is given again this often, in case the host's AI
// turned the drinker out of it.
const REFRESH_MS = 30_000;

interface Member {
    spec: StaffSpec;
    character: Character;
    /** The routine's next clip. */
    step: number;
    /** When the clip playing gives way to the next. */
    until: number;
    /** Busy serving or drinking to a round until then. */
    servingUntil: number;
}

interface Venue {
    staff: Member[];
    emptySince: number;
}

// barId -> its people
const venues = new Map<Bar['id'], Venue>();

function member(spec: StaffSpec): Member {
    const entry: Member = {
        spec,
        character: new Character({
            onReady: (npc) => {
                npc.face(spec.heading);
                entry.until = 0;
                animate(entry, Date.now());
            },
            onLost: () => {
                entry.character.respawnAt = Date.now() + RESPAWN_MS;
            },
        }),
        step: 0,
        until: 0,
        servingUntil: 0,
    };

    return entry;
}

function venue(bar: Bar): Venue {
    let place = venues.get(bar.id);

    if (!place) {
        place = { staff: [bar.bartender, ...bar.patrons].map(member), emptySince: 0 };
        venues.set(bar.id, place);
    }

    return place;
}

/** The mission changed: new bars, new people. */
export function forgetVenues(): void {
    venues.clear();
}

function spawnMember(bar: Bar, entry: Member): void {
    const { model, position, heading } = entry.spec;
    const npc = entry.character.spawn(model, position, heading, {
        behaviour: 'neutral',
        virtualWorld: bar.world,
    });

    entry.step = 0;
    entry.until = 0;

    if (!npc) {
        entry.character.respawnAt = Date.now() + RESPAWN_MS;
    }
}

function play(entry: Member, clip: string): void {
    entry.character.placed()?.playAnimation(clip, { loop: true });
}

// The clips loop; a routine of several moves on to the next after its time,
// a routine of one is refreshed.
function animate(entry: Member, now: number): void {
    if (!entry.character.ready || now < entry.until || now < entry.servingUntil) {
        return;
    }

    const routine = entry.spec.routine;
    const [clip, ms] = routine[entry.step % routine.length];

    entry.step++;
    play(entry, clip);
    entry.until = now + (routine.length > 1 ? ms : REFRESH_MS);
}

function staffUp(bar: Bar): void {
    if (!npcsAvailable() || (bar.built && !barRoom.isBuilt())) {
        return;
    }

    const now = Date.now();

    for (const entry of venue(bar).staff) {
        if (!entry.character.npc && now >= entry.character.respawnAt) {
            spawnMember(bar, entry);
        }
    }
}

export function standDown(bar: Bar): void {
    const place = venues.get(bar.id);

    if (!place) {
        return;
    }

    for (const entry of place.staff) {
        entry.character.remove();
        entry.character.respawnAt = 0;
    }
}

/** Staffs the bar while it has guests (and is open); stands them down once it has been empty a while. */
export function tickStaff(bar: Bar, wanted: boolean, now: number): void {
    const place = venue(bar);

    if (wanted) {
        place.emptySince = 0;
        staffUp(bar);
    } else if (place.staff.some((entry) => entry.character.npc)) {
        if (!place.emptySince) {
            place.emptySince = now;
        } else if (now - place.emptySince >= STAND_DOWN_MS) {
            standDown(bar);
        }
    }

    for (const entry of place.staff) {
        animate(entry, now);
    }
}

/** Whether someone stands behind the counter to serve. */
export function barmanIn(bar: Bar): boolean {
    return venue(bar).staff[0].character.placed() !== null;
}

/** The barman turns to the customer and pours. */
export function serve(bar: Bar, player: Player, clip = SERVING): void {
    const barman = venue(bar).staff[0];
    const npc = barman.character.placed();

    if (!npc) {
        return;
    }

    npc.face(player.position);
    play(barman, clip);
    barman.servingUntil = Date.now() + SERVE_MS;
    barman.until = 0;
    // Back to his post once the glass is down.
    setTimeout(() => barman.character.placed()?.face(barman.spec.heading), SERVE_MS);
}

/** Everyone with a glass drinks to a round. */
export function cheers(bar: Bar): void {
    const now = Date.now();

    for (const entry of venue(bar).staff) {
        if (!entry.spec.glass || !entry.character.placed()) {
            continue;
        }

        play(entry, CHEERS);
        entry.servingUntil = now + CHEERS_MS;
        entry.until = 0;
    }
}
