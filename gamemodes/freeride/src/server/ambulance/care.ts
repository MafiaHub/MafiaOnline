// Care at the scene: treating a patient where they lie (G held beside
// them), bringing back a downed player, triage at a pile-up and CPR on a
// heart attack (G tapped in a steady rhythm).
import * as jobkey from '../lib/jobkey';
import * as life from '../lib/life';
import * as profiles from '../lib/profiles';
import { RELIEF_LINES, PAIN_LINES, say } from '../lib/civilians';
import { walkOff } from '../lib/npcs';
import { COLORS, feed, name, text, toast } from '../lib/notify';
import { formatMoney, playerById } from '../lib/util';
import { calls, closeCall, pileups, presentCalls, type Call } from './board';
import { ambulanceOf, type Medic } from './duty';
import { PAY, TIP, TRIAGE_PAY, payExtra, payMedic } from './pay';
import { hospital } from './places';
import { toHospital } from './ride';

// A player brought back pays the medic once in this long: no farming one
// friend who keeps going down.
const REVIVE_PAY_EVERY_MS = 10 * 60_000;

// CPR: this many compressions, each 0.33 to 0.95 s after the last (about
// two a second, with room for the network); a pause of 2.5 s starts over
// without losing what was done, pressing too fast takes one back.
export const CPR_NEEDED = 18;
const CPR_MIN_GAP_MS = 330;
const CPR_MAX_GAP_MS = 950;

export const CPR_RESTART_MS = 2500;

// Player record -> when their revive last paid.
const revivePaid = new Map<string, number>();

export function finishTreatment(player: Player, medic: Medic, call: Call): void {
    call.medicId = player.id;

    if (call.kind === 'downed') {
        revive(player, medic, call);

        return;
    }

    // The badly hurt are treated for the ride: without an ambulance to take
    // them, they stay lying where they are and nothing is paid yet.
    if (call.kind !== 'minor' && (!ambulanceOf(medic) || !hospital())) {
        toast(
            player,
            'They need the hospital: fetch an ambulance (two stand in front of the hospital), stop it beside them and treat them again.',
            { title: 'Ambulance', tone: 'bad', seconds: 7 },
        );

        return;
    }

    const npc = call.npc;

    if (npc) {
        npc.stopAnimation();
        say(npc, RELIEF_LINES);
    }

    triage(player, medic, call);
    // A pile-up's patients are patched up on the spot, no quick tip.
    const quick = call.pileupId === null && Date.now() - call.openedAt < 4 * 60_000;

    if (call.kind === 'minor') {
        // A pile-up's seriously hurt are splinted where they sit.
        const base =
            call.triage === 'hurt' ? PAY.hurt : call.triage === 'minor' ? PAY.scraped : PAY.minor;
        const pay = base + (quick ? Math.round(base * TIP) : 0) + call.bonus;

        payMedic(player, medic, pay, 'Treated a patient');
        toast(
            player,
            `${call.triage === 'hurt' ? 'Splinted and bandaged' : 'Patched up'}${quick ? ' and thanked for the quick arrival' : ''}. ${formatMoney(pay)}.`,
            { title: 'Ambulance', tone: 'good' },
        );

        call.npc = null;
        call.resolved = true;
        closeCall(call, false);

        if (npc) {
            const { x, y, z } = npc.position;

            walkOff(npc, 20_000, { to: { x: x + 15, y, z: z + 15 } });
        }

        return;
    }

    toHospital(player, medic, call, call.kind === 'labour' ? 'Her waters broke. ' : 'Stabilised. ');
}

function revive(player: Player, medic: Medic, call: Call): void {
    const victim = playerById(call.playerId);

    calls.delete(call.id);
    presentCalls();

    if (!victim || victim.alive || !life.revive(victim, call.position, 0, 60)) {
        toast(player, 'Too late for them.', { title: 'Ambulance', tone: 'bad' });

        return;
    }

    const key = profiles.keyOf(victim) ?? `#${victim.id}`;
    const now = Date.now();

    for (const [entry, at] of revivePaid) {
        if (now - at >= REVIVE_PAY_EVERY_MS) {
            revivePaid.delete(entry);
        }
    }

    // No pay for patching up your own victim or the same patient again.
    if (call.killerId === player.id || revivePaid.has(key)) {
        medic.patients++;
        toast(
            player,
            `${victim.nickname} is back on their feet. The city pays for that once in a while, not now.`,
            { title: 'Ambulance' },
        );
    } else {
        revivePaid.set(key, now);
        payMedic(player, medic, PAY.revive, 'Brought someone back');
        toast(player, `${victim.nickname} is back on their feet. ${formatMoney(PAY.revive)}.`, {
            title: 'Ambulance',
            tone: 'good',
        });
    }

    toast(victim, `${player.nickname} patched you up.`, { title: 'Ambulance', tone: 'good' });
    feed('medic', [name(player, COLORS.green), text(' brought '), name(victim), text(' back')]);
}

// The pile-up's first patient seen to: the critical one, as it should be.
function triage(player: Player, medic: Medic, call: Call): void {
    const group = call.pileupId === null ? undefined : pileups.get(call.pileupId);

    if (!group) {
        return;
    }

    group.medics.add(player.id);

    if (group.treated++) {
        return;
    }

    if (call.triage === 'critical') {
        payExtra(player, medic, TRIAGE_PAY);
        toast(player, `The critical one first: good triage. ${formatMoney(TRIAGE_PAY)}.`, {
            title: 'Ambulance',
            tone: 'good',
            seconds: 4,
        });
    } else if (
        [...calls.values()].some(
            (other) =>
                other.pileupId === group.id &&
                other.triage === 'critical' &&
                other.state === 'waiting',
        )
    ) {
        toast(player, 'The critical patient is still bleeding: see to them next.', {
            title: 'Ambulance',
            tone: 'bad',
            seconds: 5,
        });
    }
}

/** One CPR compression: G tapped in a steady rhythm beside him until his heart starts. */
export function compress(player: Player, medic: Medic, call: Call): void {
    const now = Date.now();
    const cpr = (call.cpr ??= { count: 0, lastAt: 0, medicId: null });

    // One pair of hands on his chest: a second medic waits until the first
    // stops, then carries on from the count.
    if (cpr.medicId !== null && cpr.medicId !== player.id && now - cpr.lastAt <= CPR_RESTART_MS) {
        const other = playerById(cpr.medicId);

        toast(
            player,
            `${other?.nickname ?? 'Another medic'} is doing the CPR. Let them keep the rhythm.`,
            { title: 'Ambulance', seconds: 3 },
        );

        return;
    }

    if (cpr.medicId !== player.id) {
        cpr.medicId = player.id;
        cpr.lastAt = 0;
    }

    const gap = now - cpr.lastAt;
    let verdict = 'GOOD';

    if (!cpr.lastAt || gap > CPR_RESTART_MS) {
        verdict = 'KEEP GOING';
    } else if (gap < CPR_MIN_GAP_MS) {
        cpr.count = Math.max(0, cpr.count - 1);
        verdict = 'TOO FAST';
    } else if (gap > CPR_MAX_GAP_MS) {
        verdict = 'TOO SLOW';
    } else {
        cpr.count++;
    }

    cpr.lastAt = now;
    medic.cprAt = now;
    medic.hint = 'cpr';

    if (cpr.count < CPR_NEEDED) {
        jobkey.prompt(
            player,
            `CPR · ${verdict} · ${cpr.count}/${CPR_NEEDED}`,
            cpr.count / CPR_NEEDED,
        );

        return;
    }

    medic.cprAt = 0;
    medic.hint = null;
    jobkey.prompt(player, null);
    call.expires = null;

    if (!ambulanceOf(medic) || !hospital()) {
        toast(
            player,
            'He is breathing again, but he needs the hospital: fetch an ambulance, stop it beside him and press G.',
            { title: 'Ambulance', tone: 'gold', seconds: 7 },
        );

        return;
    }

    call.medicId = player.id;

    if (call.npc) {
        call.npc.stopAnimation();
        say(call.npc, PAIN_LINES);
    }

    toHospital(player, medic, call, 'He is breathing again! ');
}
