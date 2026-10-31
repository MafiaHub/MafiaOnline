// The calls board: every patient waiting or on the way in, the pile-ups
// they belong to, how a call ends and what everyone sees of them.
import * as firefighters from '../firefighters';
import * as life from '../lib/life';
import * as profiles from '../lib/profiles';
import { names, where } from '../lib/callouts';
import { CROUCHED, LYING, PAIN_LINES, say } from '../lib/civilians';
import { walkOff } from '../lib/npcs';
import { COLORS, crewMarkers, feed, text, toast, type Marker } from '../lib/notify';
import { events } from '../lib/lifecycle';
import { SentCache } from '../lib/players';
import {
    distance,
    flatDistance,
    formatMinutes,
    formatMoney,
    plain,
    playerById,
    vehicleById,
    type Point,
} from '../lib/util';
import { occupied } from '../lib/vehicles';
import { medics } from './duty';
import { PILEUP_PAY, payExtra } from './pay';

/**
 * downed: a player waiting for a medic; minor: patched up where they are;
 * serious, labour, cardiac: ride to the hospital.
 */
export type CallKind = 'downed' | 'minor' | 'serious' | 'labour' | 'cardiac';
/** waiting where they are; pickup, boarding and aboard on the way in. */
export type CallState = 'waiting' | 'pickup' | 'boarding' | 'aboard';
export type Triage = 'critical' | 'hurt' | 'minor';

/** CPR on a heart attack (care.ts): one pair of hands at a time. */
export interface Cpr {
    count: number;
    lastAt: number;
    medicId: number | null;
}

/** The ride in, from the moment the patient is the medic's to take. */
export interface Ride {
    deadline: number;
    from: Point;
    crashes: number;
    joltAt: number;
}

export interface Call {
    id: number;
    kind: CallKind;
    position: Point;
    label: string;
    npc: Npc | null;
    /** The downed player, and who put them down. */
    playerId: number | null;
    killerId: number | null;
    state: CallState;
    openedAt: number;
    /** The medic who treated them. */
    medicId: number | null;
    generation: number;
    where: string;
    nextLineAt: number;
    /** The clip they wait in; by kind when null. */
    clip: string | null;
    /** When an urgent patient is lost (a heart attack, a pile-up's critical one). */
    expires: number | null;
    triage: Triage | null;
    pileupId: number | null;
    /** A share of a big fire call's bonus, paid with the fare. */
    bonus: number;
    /** The host made the NPC. */
    ready: boolean;
    lying: boolean;
    /** Seen to (not lost or given up on). */
    resolved: boolean;
    cpr: Cpr | null;
    /** The getting-in task of the boarding patient. */
    task: number;
    boardSince: number;
    ride: Ride | null;
}

/** Two wrecks across the lane and three people hurt around them. */
export interface Pileup {
    id: number;
    position: Point;
    where: string;
    wrecks: number[];
    callIds: Set<number>;
    /** How many of its patients were treated so far. */
    treated: number;
    /** Who treated them. */
    medics: Set<number>;
    missed: number;
    /** The fire brigade's call for the burning wreck. */
    fireId: number | null;
    /** When the wrecks go, once every patient is seen to. */
    clearAt: number;
    openedAt: number;
}

export const DOWN_MS = 60_000;
const TREAT_REACH = 3;
const CALL_LIFETIME_MS = 8 * 60_000;
const BOARD_TIMEOUT_MS = 15_000;
const MOAN_EVERY_MS = 11_000;
// The pile-up's wrecks are towed this long after the last patient is seen to.
const WRECK_CLEAR_MS = 30_000;

export const TRIAGE: Readonly<Record<Triage, { label: string; order: number }>> = {
    critical: { label: 'CRITICAL', order: 0 },
    hurt: { label: 'SERIOUS', order: 1 },
    minor: { label: 'MINOR', order: 2 },
};

export const calls = new Map<number, Call>();
export const pileups = new Map<number, Pileup>();
let nextCall = 1;
let nextPileup = 1;
// The call markers last sent to each player (lib/notify.ts crewMarkers).
const sent = new SentCache();

export function callOf(id: number | null): Call | undefined {
    return id === null ? undefined : calls.get(id);
}

function callLabel(call: Call): string {
    if (call.kind === 'downed') {
        return `MAN DOWN · ${call.label}`;
    }

    if (call.triage) {
        return TRIAGE[call.triage].label;
    }

    return (
        { serious: 'BADLY HURT', labour: 'IN LABOUR', cardiac: 'HEART ATTACK', minor: null }[
            call.kind
        ] ?? 'INJURED'
    );
}

function triageOrder(call: Call): number {
    return call.triage ? TRIAGE[call.triage].order : 0;
}

export function presentCalls(): void {
    const open = [...calls.values()].filter((call) => call.state === 'waiting');
    // A pile-up has one beacon and blip, on its most urgent patient left.
    const leads = new Set<number>();

    for (const group of pileups.values()) {
        const lead = open
            .filter((call) => call.pileupId === group.id)
            .sort((a, b) => triageOrder(a) - triageOrder(b))[0] as Call | undefined;

        if (lead) {
            leads.add(lead.id);
        }
    }

    const items: Marker[] = open.map((call) => {
        const grouped = call.pileupId !== null;
        const lead = leads.has(call.id);
        let map: unknown;

        if (grouped) {
            map = lead ? { icon: 'diamond', label: 'Pile-up', flash: true } : false;
        } else {
            map = {
                icon: 'diamond',
                label: call.kind === 'downed' ? 'Man down' : 'Patient',
                flash: call.kind === 'downed' || call.kind === 'cardiac',
            };
        }

        return {
            id: `call${call.id}`,
            position: call.position,
            label: callLabel(call),
            color: call.triage === 'critical' ? COLORS.red : COLORS.green,
            beacon: !grouped || lead,
            range: 900,
            near: grouped ? 60 : 120,
            map,
        };
    });

    // Everyone sees the patients; only medics on duty get them on the map.
    crewMarkers('medic', sent, items, (player) => medics.has(player));
}

function callMessage(call: Call): string {
    switch (call.kind) {
        case 'downed':
            return `${call.label} is down ${call.where}.`;
        case 'serious':
            return `Someone is badly hurt ${call.where}.`;
        case 'labour':
            return `A woman has gone into labour ${call.where}. Get her to the hospital quickly, and gently: every bump costs you the tip.`;
        case 'cardiac':
            return (
                `A man collapsed with a heart attack ${call.where}. He has ${formatMinutes((call.expires ?? 0) - Date.now())}: ` +
                'give him CPR, G tapped steadily beside him.'
            );
        default:
            return `Someone collapsed ${call.where}.`;
    }
}

/** What a kind of call needs; quiet opens it without a toast or feed line of its own. */
export interface CallDetails {
    npc?: Npc | null;
    playerId?: number | null;
    killerId?: number | null;
    clip?: string | null;
    expires?: number | null;
    triage?: Triage | null;
    pileupId?: number | null;
    bonus?: number;
    quiet?: boolean;
}

export function openCall(
    kind: CallKind,
    position: Point,
    label: string,
    { quiet = false, ...details }: CallDetails = {},
): Call {
    const now = Date.now();
    const call: Call = {
        id: nextCall++,
        kind,
        position: plain(position),
        label,
        npc: details.npc ?? null,
        playerId: details.playerId ?? null,
        killerId: details.killerId ?? null,
        state: 'waiting',
        openedAt: now,
        medicId: null,
        generation: World.getMissionGeneration(),
        where: where(position),
        nextLineAt: now + 4000,
        clip: details.clip ?? null,
        expires: details.expires ?? null,
        triage: details.triage ?? null,
        pileupId: details.pileupId ?? null,
        bonus: details.bonus ?? 0,
        ready: false,
        lying: false,
        resolved: false,
        cpr: null,
        task: 0,
        boardSince: 0,
        ride: null,
    };

    calls.set(call.id, call);

    if (!quiet) {
        for (const [medic] of medics.crew()) {
            toast(medic, callMessage(call), {
                title: 'Ambulance call',
                tone: 'gold',
                seconds: kind === 'labour' || kind === 'cardiac' ? 8 : 6,
            });
        }

        feed('medic', [
            text('Ambulance', COLORS.green),
            text(
                ` · ${kind === 'downed' ? `${label} needs a medic` : 'a call came in'} ${call.where}`,
            ),
        ]);
    }

    presentCalls();

    return call;
}

export function openPileup(position: Point, wrecks: number[]): Pileup {
    const group: Pileup = {
        id: nextPileup++,
        position,
        where: where(position),
        wrecks,
        callIds: new Set(),
        treated: 0,
        medics: new Set(),
        missed: 0,
        fireId: null,
        clearAt: 0,
        openedAt: Date.now(),
    };

    pileups.set(group.id, group);

    return group;
}

/** A pile-up's patient is seen to, lost or given up on: the last of them settles the pile-up. */
export function leaveGroup(call: Call): void {
    const group = call.pileupId === null ? undefined : pileups.get(call.pileupId);

    if (!group || !group.callIds.delete(call.id)) {
        return;
    }

    if (!call.resolved) {
        group.missed++;
    }

    if (!group.callIds.size) {
        finishPileup(group);
    }
}

/**
 * Every patient of the pile-up seen to: the medics who were there share in
 * it if nobody was lost or left. The wrecks go a little later.
 */
function finishPileup(group: Pileup): void {
    group.clearAt = Date.now() + WRECK_CLEAR_MS;
    const team = [...group.medics].flatMap((id) => {
        const player = playerById(id);
        const medic = player ? medics.get(player) : undefined;

        return player && medic ? [{ player, medic }] : [];
    });

    if (group.missed || !team.length) {
        return;
    }

    const cut = Math.max(25, Math.round(PILEUP_PAY / team.length));

    for (const { player, medic } of team) {
        payExtra(player, medic, cut);
        profiles.addXp(player, 40, 'Cleared a pile-up');
        toast(
            player,
            `Everyone from the pile-up is seen to. ${formatMoney(cut)} more${team.length > 1 ? ' (your share)' : ''}.`,
            { title: 'Ambulance', tone: 'good' },
        );
    }

    feed('medic', [
        ...names(
            team.map(({ player }) => player),
            COLORS.green,
        ),
        text(` cleared the pile-up ${group.where}`),
    ]);
}

/** The wrecks are towed once nobody sits in them. */
export function clearWrecks(group: Pileup): void {
    pileups.delete(group.id);

    for (const id of group.wrecks) {
        const wreck = vehicleById(id);

        if (wreck && !occupied(wreck)) {
            wreck.destroy();
        }
    }
}

export function closeCall(call: Call, removeNpc = true): void {
    calls.delete(call.id);
    leaveGroup(call);

    if (removeNpc && call.npc) {
        const npc = call.npc;

        call.npc = null;
        npc.remove();
    }

    if (call.kind === 'downed' && call.state === 'waiting') {
        const player = playerById(call.playerId);

        if (player) {
            life.holdRespawn(player, null);
        }
    }

    presentCalls();
}

// Too late: the patient dies where they lie, and is taken away later.
function lose(call: Call): void {
    for (const [medic] of medics.crew()) {
        toast(
            medic,
            `Too late: the ${call.kind === 'cardiac' ? 'heart attack' : 'crash victim'} ${call.where} did not make it.`,
            { title: 'Ambulance', tone: 'bad', seconds: 5 },
        );
    }

    feed('medic', [
        text('Ambulance', COLORS.muted),
        text(` · a patient ${call.where} did not make it`),
    ]);

    const npc = call.npc;

    call.npc = null;
    closeCall(call, false);

    if (npc) {
        walkOff(npc, 15_000);
    }
}

/** Carried in on the stretcher: the patient rides out of sight. */
export function stretcher(call: Call): void {
    call.state = 'aboard';

    if (call.npc) {
        const npc = call.npc;

        call.npc = null;
        npc.remove();
    }
}

/** The waiting patient within reach of the player's hands. */
export function callNear(player: Player): Call | null {
    for (const call of calls.values()) {
        if (call.state !== 'waiting') {
            continue;
        }

        const target =
            call.kind === 'downed'
                ? (playerById(call.playerId)?.position ?? call.position)
                : (call.npc?.position ?? call.position);

        if (distance(target, player.position) <= TREAT_REACH) {
            return call;
        }
    }

    return null;
}

export function nearestCall(position: Point): { call: Call; gap: number } | null {
    return (
        [...calls.values()]
            .filter((call) => call.state === 'waiting')
            .map((call) => ({ call, gap: flatDistance(call.position, position) }))
            .sort((a, b) => a.gap - b.gap)[0] ?? null
    );
}

/** Whether a medic other than this player is on duty. */
export function otherMedic(playerId: number | null): boolean {
    return medics.crew().some(([medic]) => medic.id !== playerId);
}

/** A medic goes off duty: their patient is off, and downed players wait only while someone can come. */
export function releaseMedic(player: Player, patient: number | null): void {
    const call = callOf(patient);

    if (call) {
        closeCall(call);
    }

    for (const open of calls.values()) {
        if (open.medicId === player.id && open.state === 'waiting') {
            open.medicId = null;
        }
    }

    for (const open of [...calls.values()]) {
        if (open.kind === 'downed' && !otherMedic(open.playerId)) {
            closeCall(open);
        }
    }
}

/** Each tick: patients lie down, downed players and urgent ones run out of time, the rest moan. */
export function updateCalls(now: number): void {
    for (const call of [...calls.values()]) {
        if (call.generation !== World.getMissionGeneration()) {
            calls.delete(call.id);
            continue;
        }

        if (call.state === 'waiting' && call.npc && call.ready && !call.lying) {
            call.lying = true;
            call.npc.playAnimation(call.clip ?? (call.kind === 'minor' ? CROUCHED : LYING), {
                loop: true,
            });
        }

        if (call.kind === 'downed') {
            const victim = playerById(call.playerId);

            if (!victim || victim.alive || now - call.openedAt > DOWN_MS) {
                if (victim && !victim.alive) {
                    toast(victim, 'No medic made it in time.', { title: 'Ambulance' });
                }

                closeCall(call);
            }

            continue;
        }

        if (call.state === 'boarding' && now - call.boardSince > BOARD_TIMEOUT_MS) {
            stretcher(call);
        }

        // A heart attack or a pile-up's critical patient left too long.
        if (call.state === 'waiting' && call.expires && now > call.expires) {
            lose(call);
            continue;
        }

        if (call.state === 'waiting' && now - call.openedAt > CALL_LIFETIME_MS) {
            closeCall(call);
            continue;
        }

        // They moan while they wait (the heart attack is out cold); a woman
        // in labour more often, in the ambulance too.
        const labour = call.kind === 'labour' && call.state !== 'waiting';

        if (
            call.npc &&
            call.ready &&
            call.kind !== 'cardiac' &&
            (call.state === 'waiting' || labour) &&
            now >= call.nextLineAt
        ) {
            call.nextLineAt =
                now +
                MOAN_EVERY_MS * (call.kind === 'labour' ? 0.5 : 1) * (0.7 + Math.random() * 0.6);

            say(call.npc, PAIN_LINES);
        }
    }

    // A pile-up's wrecks go once its patients are seen to and its fire is out.
    for (const group of [...pileups.values()]) {
        const burning =
            group.fireId !== null &&
            firefighters.incidents().some((incident) => incident.id === group.fireId);

        if (group.clearAt && now >= group.clearAt && !burning) {
            clearWrecks(group);
        }
    }
}

/** Every call ends and the wrecks go: the resource stops. */
export function endAll(): void {
    for (const call of [...calls.values()]) {
        closeCall(call);
    }

    for (const group of [...pileups.values()]) {
        clearWrecks(group);
    }
}

/** The mission change removed every patient and wreck. */
export function forgetAll(): void {
    calls.clear();
    pileups.clear();
    sent.clear();
}

events.on('npcSpawn', (npc) => {
    for (const call of calls.values()) {
        if (call.npc?.id === npc.id) {
            call.ready = true;
        }
    }
});

events.on('npcDeath', (npc) => {
    for (const call of [...calls.values()]) {
        if (call.npc?.id !== npc.id) {
            continue;
        }

        const medic = playerById(call.medicId);
        const state = medic ? medics.get(medic) : undefined;

        if (state && state.patient === call.id) {
            state.patient = null;
        }

        feed('medic', [
            text('Ambulance', COLORS.muted),
            text(` · a patient ${call.where} did not make it`),
        ]);

        closeCall(call, false);
    }
});

// A patient the host could not make or lost: a street call is off, one
// already in the medic's hands rides in on the stretcher.
events.on('npcRetire', (npc) => {
    for (const call of [...calls.values()]) {
        if (call.npc?.id !== npc.id) {
            continue;
        }

        call.npc = null;

        if (call.state === 'waiting') {
            closeCall(call, false);
        } else {
            call.state = 'aboard';
        }
    }
});

// The calls open before this player came go out on the next tick.
events.on('playerSpawn', (player) => sent.delete(player.id));

// A downed player who leaves waits no more.
events.on('playerDisconnect', (player) => {
    for (const call of [...calls.values()]) {
        if (call.kind === 'downed' && call.playerId === player.id) {
            calls.delete(call.id);
        }
    }
});
