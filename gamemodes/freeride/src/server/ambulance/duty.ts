// Ambulance duty: the station at the hospital, the shift and a medic's
// rounds: the compass, the patient on the way in, treating and the card.
import * as jobkey from '../lib/jobkey';
import * as stations from '../lib/stations';
import { defineShift } from '../lib/shift';
import { COLORS, card, compass, feed, name, text, toast } from '../lib/notify';
import {
    flatDistance,
    formatMinutes,
    formatMoney,
    inFreeRide,
    playable,
    plural,
    speedOf,
    vehicleById,
} from '../lib/util';
import {
    calls,
    callNear,
    callOf,
    closeCall,
    nearestCall,
    presentCalls,
    releaseMedic,
    TRIAGE,
    updateCalls,
    type Call,
} from './board';
import { CPR_NEEDED, CPR_RESTART_MS, compress, finishTreatment } from './care';
import { AMBULANCE, hospital } from './places';
import { boardAmbulance, deliver, jolt } from './ride';
import { clock, dispatch } from './scenes';

const TREAT_MS = 5000;
const REVIVE_MS = 4000;
const BAY_RADIUS = 22;
const BAY_SPEED = 3;
const TICK_MS = 250;
const CARD_MS = 1000;
const COMPASS_PRIORITY = 43;
// A drop in speed this sharp with a woman in labour aboard is a bump.
const CRASH_SPEED_DROP = 9;

/** One medic's shift. */
export interface Medic {
    /** The ambulance the medic drove last carries their patients. */
    ambulanceId: number | null;
    earned: number;
    patients: number;
    /** The call they are taking in. */
    patient: number | null;
    treating: { callId: number; startedAt: number } | null;
    hint: string | null;
    /** The last CPR compression; its prompt stays while they keep at it. */
    cprAt: number;
    lastSpeed: number | null;
}

export const station = stations.define({
    id: 'medic',
    title: 'Ambulance',
    crew: 'medics',
    mapLabel: 'Hospital',
    color: COLORS.green,
    radar: 0x8fc27a,
    model: AMBULANCE,
    vehicleLabel: 'AMBULANCE',
    pitch:
        'Answer calls around the city: treat people where they lie, drive the badly hurt here quickly for a tip, and bring back players ' +
        'who went down. $140 to $460 a patient, about $500 to $700 for a birth, a heart attack or a pile-up. Press G here to sign on.',
    place: hospital,
    available: () => true,
    onDuty: (player) => medics.has(player),
    start: (player) => medics.start(player),
    stop: (player) => medics.stop(player, 'Off duty. The ambulances stay at the hospital.'),
    onDrive: (player, vehicle) => {
        const medic = medics.get(player);
        // A patient getting in or aboard keeps their ambulance; one still
        // waiting at the roadside goes into whichever the medic brings.
        const state = callOf(medic?.patient ?? null)?.state;

        if (medic && state !== 'boarding' && state !== 'aboard') {
            medic.ambulanceId = vehicle.id;
        }
    },
});

export const medics = defineShift<Medic>({
    kind: 'medic',
    activity: 'Ambulance duty',
    title: 'Ambulance',
    hud: 'medic',
    compassPriority: COMPASS_PRIORITY,
    flag: { key: 'fr.job', value: 'medic' },
    station: {
        station,
        missing: 'There is no hospital out here.',
        directions: () =>
            `Sign on at ${hospital()?.name ?? 'the hospital'}: follow the compass to the arrow at the foot of the steps and press G.`,
    },
    interrupted: 'Your shift is over.',
    tickMs: TICK_MS,

    begin: () => ({
        ambulanceId: null,
        earned: 0,
        patients: 0,
        patient: null,
        treating: null,
        hint: null,
        cprAt: 0,
        lastSpeed: null,
    }),

    started(player) {
        feed('medic', [name(player, COLORS.green), text(' went on ambulance duty')]);
        toast(
            player,
            'You are on duty. Take one of the ambulances parked in front of the hospital. Calls show on your compass and on the map (TAB). ' +
                'Hold G beside a patient to treat them; the badly hurt ride with you to the hospital, quickly for a tip. ' +
                'Players who go down now wait a minute for you. Back under the arrow, G clocks you off.',
            { title: 'Ambulance', tone: 'gold', seconds: 12 },
        );

        if (!calls.size) {
            clock.within(20_000);
        }
    },

    // New calls now and then, and the calls running their course.
    beforeTick(now) {
        if (!inFreeRide() || !World.isReady()) {
            return;
        }

        dispatch(now);
        updateCalls(now);
    },

    tick: (player, medic, now) => rounds(player, medic, now),
    afterTick: (now) => present(now),
    end: (player, medic) => releaseMedic(player, medic.patient),
    summary: (medic) => `${plural(medic.patients, 'patient')}, ${formatMoney(medic.earned)}.`,
});

export function ambulanceOf(medic: Medic): Vehicle | null {
    return vehicleById(medic.ambulanceId);
}

// The compass, the patient on the way in, treating and the prompt.
function rounds(player: Player, medic: Medic, now: number): void {
    if (!inFreeRide() || !World.isReady() || !playable(player)) {
        return;
    }

    const call = callOf(medic.patient);

    if (call && call.state === 'pickup') {
        compass(player, 'medic', COMPASS_PRIORITY, {
            position: call.npc?.position ?? call.position,
        });

        boardAmbulance(player, medic, call);
    } else if (call && (call.state === 'aboard' || call.state === 'boarding')) {
        takeIn(player, medic, call);
    } else {
        const nearest = nearestCall(player.position);

        compass(
            player,
            'medic',
            COMPASS_PRIORITY,
            nearest ? { position: nearest.call.position } : null,
        );
    }

    // Treating takes a few seconds of G beside the patient.
    if (medic.treating) {
        treat(player, medic, medic.treating, now);

        return;
    }

    // CPR keeps its own prompt while the medic keeps tapping.
    if (medic.cprAt && now - medic.cprAt < CPR_RESTART_MS) {
        return;
    }

    medic.cprAt = 0;
    const near = player.getVehicle() ? null : callNear(player);
    let hint: string | null = null;

    if (near?.kind === 'downed') {
        hint = '[G] HOLD TO REVIVE';
    } else if (near?.kind === 'cardiac') {
        hint = '[G] TAP STEADILY FOR CPR';
    } else if (near) {
        hint = '[G] HOLD TO TREAT';
    }

    if (hint !== medic.hint) {
        medic.hint = hint;
        jobkey.prompt(player, hint);
    }
}

// The patient is getting in or aboard: to the bay, and every bump with a
// woman in labour aboard costs the smooth tip.
function takeIn(player: Player, medic: Medic, call: Call): void {
    const home = hospital();
    const ambulance = ambulanceOf(medic);
    const driving = ambulance !== null && player.getVehicle()?.id === ambulance.id;

    if (home) {
        compass(player, 'medic', COMPASS_PRIORITY, { position: home.bay });
    }

    if (call.kind === 'labour' && ambulance && driving) {
        const speed = speedOf(ambulance);

        if ((medic.lastSpeed ?? speed) - speed >= CRASH_SPEED_DROP) {
            jolt(player, call);
        }

        medic.lastSpeed = speed;
    }

    if (!ambulance) {
        toast(player, 'The ambulance is gone, and the patient with it.', { tone: 'bad' });
        medic.patient = null;
        closeCall(call);
    } else if (
        call.state === 'aboard' &&
        call.ride &&
        home &&
        driving &&
        flatDistance(ambulance.position, home.bay) <= BAY_RADIUS &&
        speedOf(ambulance) < BAY_SPEED
    ) {
        deliver(player, medic, call, call.ride, home);
    }
}

function treat(
    player: Player,
    medic: Medic,
    treating: NonNullable<Medic['treating']>,
    now: number,
): void {
    const call = calls.get(treating.callId);
    const reach = call && callNear(player)?.id === call.id;

    if (!call || !reach || !jobkey.isHeld(player) || player.getVehicle()) {
        medic.treating = null;
        jobkey.prompt(player, null);
        medic.hint = null;

        return;
    }

    const needed = call.kind === 'downed' ? REVIVE_MS : TREAT_MS;
    const progress = Math.min(1, (now - treating.startedAt) / needed);

    jobkey.prompt(player, call.kind === 'downed' ? 'REVIVING…' : 'TREATING…', progress);
    medic.hint = 'busy';

    if (progress >= 1) {
        medic.treating = null;
        jobkey.prompt(player, null);
        medic.hint = null;
        finishTreatment(player, medic, call);
    }
}

/** G for a medic: treat or revive the patient beside them, CPR for a heart attack; a downed player gives up waiting. */
export const medicKey: jobkey.JobKeyHandler = {
    press(player) {
        if (!player.alive) {
            const call = [...calls.values()].find(
                (entry) => entry.kind === 'downed' && entry.playerId === player.id,
            );

            if (!call) {
                return false;
            }

            closeCall(call);
            toast(player, 'You stop waiting for a medic.', { title: 'Ambulance' });

            return true;
        }

        const medic = medics.get(player);

        if (!medic || !playable(player) || player.getVehicle()) {
            return false;
        }

        const call = callNear(player);

        if (!call) {
            return false;
        }

        if (call.kind === 'cardiac') {
            compress(player, medic, call);
        } else {
            medic.treating = { callId: call.id, startedAt: Date.now() };
        }

        return true;
    },

    release(player) {
        const medic = medics.get(player);

        if (medic) {
            medic.treating = null;
        }
    },
};

let lastCardAt = 0;

// The markers and each medic's card, once a second.
function present(now: number): void {
    if (!inFreeRide() || !World.isReady() || now - lastCardAt < CARD_MS) {
        return;
    }

    lastCardAt = now;
    presentCalls();

    for (const [player, medic] of medics.crew()) {
        card(player, 'medic', { title: 'Ambulance', ...cardOf(player, medic, now), tone: 'good' });
    }
}

const KIND_ROWS: Readonly<Record<Call['kind'], string>> = {
    downed: 'Man down',
    serious: 'Badly hurt',
    labour: 'In labour',
    cardiac: 'Heart attack',
    minor: 'Collapsed',
};

function cardOf(
    player: Player,
    medic: Medic,
    now: number,
): { subtitle: string; rows: [string, string][] } {
    const call = callOf(medic.patient);
    const nearest = nearestCall(player.position);
    const rows: [string, string][] = [];

    if (call) {
        rows.push([
            'Patient',
            call.state === 'pickup'
                ? 'waiting for the ambulance'
                : call.state === 'boarding'
                  ? 'getting in'
                  : 'aboard',
        ]);

        if (call.ride) {
            const deadline = call.ride.deadline;

            rows.push(['Tip window', deadline > now ? formatMinutes(deadline - now) : 'missed']);

            if (call.kind === 'labour') {
                rows.push([
                    'Ride',
                    call.ride.crashes ? plural(call.ride.crashes, 'bump') : 'smooth so far',
                ]);
            }
        }

        const home = hospital();

        if (home) {
            rows.push(['Hospital', `${Math.round(flatDistance(player.position, home.bay))} m`]);
        }
    } else if (nearest) {
        const open = nearest.call;

        rows.push([
            open.triage
                ? `Pile-up · ${TRIAGE[open.triage].label.toLowerCase()}`
                : KIND_ROWS[open.kind],
            `${Math.round(nearest.gap)} m`,
        ]);

        if (open.expires) {
            rows.push([
                open.kind === 'cardiac' ? 'His time' : 'Bleeding out in',
                formatMinutes(Math.max(0, open.expires - now)),
            ]);
        }

        if (open.cpr?.count) {
            rows.push(['CPR', `${open.cpr.count}/${CPR_NEEDED}`]);
        }
    }

    rows.push([
        'Open calls',
        `${[...calls.values()].filter((entry) => entry.state === 'waiting').length}`,
    ]);

    rows.push(['This shift', `${medic.patients} · ${formatMoney(medic.earned)}`]);

    return { subtitle: subtitleOf(medic, call, nearest?.call ?? null), rows };
}

function subtitleOf(medic: Medic, call: Call | undefined, nearest: Call | null): string {
    if (call) {
        return call.kind === 'labour'
            ? 'To the hospital, quickly and gently'
            : 'Get the patient to the hospital';
    }

    if (!ambulanceOf(medic)) {
        return 'Take an ambulance from the hospital';
    }

    if (nearest?.kind === 'cardiac') {
        return 'CPR: tap G steadily beside him';
    }

    if (nearest?.pileupId) {
        return 'Pile-up: the critical one first';
    }

    return nearest ? `A call ${nearest.where}` : 'No calls. Wait by the hospital.';
}
