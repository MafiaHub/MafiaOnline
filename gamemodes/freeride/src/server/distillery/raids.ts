// Visitors to the camp. At full heat the revenue men come for the still,
// after a warning: defend it or bribe them off, or they seize everything
// stored there. Morello's boys come for a big stock of cases. Sergeant Doyle
// of the county police walks up now and then while the heat is high, and
// forgets what he saw for an envelope.
import * as life from '../lib/life';
import * as profiles from '../lib/profiles';
import { Character, npcsAvailable, speak, type Loss } from '../lib/npcs';
import { feed, text, toast, COLORS } from '../lib/notify';
import {
    distance,
    flatDistance,
    formatMinutes,
    formatMoney,
    headingFrom,
    MAIN_WORLD,
    onStreet,
    pick,
    playerById,
    plural,
    type Point,
} from '../lib/util';
import * as interiors from '../interiors';
import { hankSays, LINE } from './characters';
import { addHeat, DOYLE_MIN, DOYLE_PER_HEAT, HEAT_DOYLE, HEAT_REFUSED } from './heat';
import {
    changed,
    cooldownOf,
    members,
    opKeyOf,
    ops,
    RUM,
    setWanted,
    takeCases,
    tellOp,
    TITLE,
    XP,
    type Operation,
} from './operations';
import { CAMP_RADIUS, CRATES, HANK_RANGE, POT, RAID_POINTS } from './places';

interface Gunman {
    model: string;
    weapon: number;
    ammo: number;
    reserve: number;
}

const RIVALS: readonly Gunman[] = [
    { model: 'Enemy04K.i3d', weapon: 10, ammo: 50, reserve: 100 },
    { model: 'Enemy12K.i3d', weapon: 6, ammo: 6, reserve: 30 },
    { model: 'Gangster01.i3d', weapon: 6, ammo: 6, reserve: 30 },
];
const AGENTS: readonly Gunman[] = [
    { model: 'pol01.i3d', weapon: 11, ammo: 8, reserve: 24 },
    { model: 'pol02.i3d', weapon: 6, ammo: 8, reserve: 32 },
    { model: 'pol01.i3d', weapon: 6, ammo: 8, reserve: 32 },
];
// Retail voice ids (Npc.spawn).
const RIVAL_VOICE = 14;
const AGENT_VOICE = 11;
const DOYLE = { model: 'pol01.i3d', voice: 10 };

// At full heat the revenue men give a member on the street this long to
// reach the still (time to drive there from the docks) or buy them off; a
// member at the camp meets them once the first minute is up.
const RAID_WINDOW_MS = 4.5 * 60_000;
const RAID_PREP_MS = 60_000;
const RAID_RESUME_MS = 2 * 60_000;

export const RAID_DEFEND = 250;
const RAID_TIMEOUT_MS = 240_000;
const LOST_GRACE_MS = 30_000;

export const ARRIVE = 3;
const RIVAL_CHANCE = 0.15;
const RIVAL_MIN_CASES = 6;
const RIVAL_STEAL = 6;
const DOYLE_CHANCE = 0.25;
const DOYLE_WAIT_MS = 60_000;
const DOYLE_LEAVE_MS = 15_000;
const OP_EVENT_GAP_MS = 12 * 60_000;
// Activities (lib/life.ts) that keep a member from answering a raid.
const AWAY = new Set<string>(['duel', 'race', 'jail', 'visit']);

type RaidKind = 'revenue' | 'rivals';

interface Raider {
    character: Character;
    /** A defender's player id, or the crates when nobody defends. */
    targetId: number | 'crates' | null;
    state: 'alive' | 'dead' | 'gone';
}

interface Raid {
    kind: RaidKind;
    opKey: string;
    npcs: Raider[];
    startedAt: number;
    lostSince: number;
}

interface Doyle {
    opKey: string;
    character: Character;
    targetId: number;
    price: number;
    startedAt: number;
    walkAt: number;
    metAt: number;
}

/** Revenue men on their way to an operation's still; not saved. */
export interface RevenueCall {
    startedAt: number;
    dueAt: number;
    pausedAt: number;
}

let raid: Raid | null = null;
let doyle: Doyle | null = null;
// opKey -> revenue men on their way
const raidCalls = new Map<string, RevenueCall>();

export function revenueCall(opKey: string | null): RevenueCall | null {
    return opKey === null ? null : (raidCalls.get(opKey) ?? null);
}

/** A bribe sent them back. */
export function callOffRevenue(opKey: string): boolean {
    return raidCalls.delete(opKey);
}

export function defendersOf(opKey: string, reach: number): Player[] {
    return members(opKey).filter(
        (player) => onStreet(player) && flatDistance(player.position, POT) <= reach,
    );
}

function spawnHostile(
    raider: Raider,
    spec: Gunman,
    point: Point,
    index: number,
    voice: number,
): Npc | null {
    const offset = { x: point.x + (index % 2 ? 1.5 : -1.5), y: point.y + 0.2, z: point.z + index };

    return raider.character.spawn(spec.model, offset, headingFrom(point, POT), {
        behaviour: 'hostile',
        health: 120,
        weapon: spec.weapon,
        ammo: spec.ammo,
        reserve: spec.reserve,
        voice,
        virtualWorld: MAIN_WORLD,
    });
}

function raiderLost(raider: Raider, loss: Loss): void {
    // Retired by the host without a fight counts as them leaving.
    raider.state = loss.died ? 'dead' : 'gone';
}

function startRaid(kind: RaidKind, opKey: string): boolean {
    if (raid || !npcsAvailable()) {
        return false;
    }

    const specs = kind === 'revenue' ? AGENTS : RIVALS;
    const point = pick(RAID_POINTS);
    const npcs = specs
        .map((spec, index) => {
            const raider: Raider = {
                character: new Character({ onLost: (_npc, loss) => raiderLost(raider, loss) }),
                targetId: null,
                state: 'alive',
            };

            spawnHostile(
                raider,
                spec,
                point,
                index,
                kind === 'revenue' ? AGENT_VOICE : RIVAL_VOICE,
            );

            return raider;
        })
        .filter((raider) => raider.character.npc);

    if (!npcs.length) {
        return false;
    }

    raid = { kind, opKey, npcs, startedAt: Date.now(), lostSince: 0 };
    cooldownOf(opKey).raidAt = Date.now();
    const op = ops.get(opKey);

    if (kind === 'revenue') {
        tellOp(
            opKey,
            'The revenue men are at your still! Drive them off, or they seize everything stored there.',
            'bad',
            9,
        );

        for (const player of defendersOf(opKey, RAID_DEFEND)) {
            setWanted(player, 1);
        }

        feed('rum', [
            text('Revenue raid', COLORS.red),
            text(` · on ${op?.name ?? 'someone'}'s still in the lowland woods`),
        ]);
    } else {
        tellOp(
            opKey,
            "Morello's boys heard about your rum and came to take it. Defend the cases!",
            'bad',
            9,
        );
    }

    for (const player of defendersOf(opKey, HANK_RANGE)) {
        hankSays(
            player,
            kind === 'revenue' ? 'Revenuers! Grab a gun!' : "Morello's boys! Here they come!",
            LINE.fight,
            5,
        );
    }

    return true;
}

function endRaid(outcome: 'won' | 'lost' | 'timeout'): void {
    const current = raid;

    if (!current) {
        return;
    }

    raid = null;

    for (const entry of current.npcs) {
        if (entry.state === 'alive') {
            entry.character.remove();
        }
    }

    const op = ops.get(current.opKey);

    if (!op) {
        return;
    }

    const revenue = current.kind === 'revenue';

    if (outcome === 'won') {
        if (revenue) {
            op.heat = 60;
        }

        changed();

        for (const player of defendersOf(current.opKey, RAID_DEFEND)) {
            const reward = revenue ? 100 : 150;

            player.giveMoney(reward);
            profiles.addXp(player, XP.defend, 'Defended the still');
            toast(
                player,
                revenue
                    ? `The revenue men are down. They'll be back with more if you don't lay low. ${formatMoney(reward)} found on them.`
                    : `Morello's boys won't try that again. ${formatMoney(reward)} in their pockets.`,
                { title: TITLE, tone: 'good', seconds: 8 },
            );
        }

        feed('rum', [
            text(op.name, RUM),
            text(
                revenue
                    ? ' fought off a revenue raid'
                    : " drove Morello's boys away from their still",
            ),
        ]);

        return;
    }

    if (outcome === 'lost') {
        if (revenue) {
            op.casks = [];
            op.cases = { n: 0, q: 0 };
            op.molasses = 0;
            op.heat = 30;
            tellOp(
                current.opKey,
                'The revenue men smashed the casks and took every case and sack. The heat is off for now.',
                'bad',
                9,
            );

            feed('rum', [text('Revenue raid', COLORS.red), text(` · ${op.name}'s rum was seized`)]);
        } else {
            const taken = takeCases(op.cases, Math.min(RIVAL_STEAL, op.cases.n));

            tellOp(
                current.opKey,
                `Morello's boys carried off ${plural(taken.n, 'case')} of your rum.`,
                'bad',
            );
        }

        changed();

        return;
    }

    // Ran out of time: they leave.
    if (revenue) {
        op.heat = Math.max(op.heat - 30, 70);
    }

    tellOp(
        current.opKey,
        revenue ? 'The revenue men gave up for today.' : "Morello's boys gave up and left.",
    );
}

export function tickRaid(now: number): void {
    if (!raid) {
        return;
    }

    const alive = raid.npcs.filter((entry) => entry.state === 'alive');

    if (!alive.length) {
        // Retired by the host without a fight counts as them leaving.
        endRaid(raid.npcs.some((entry) => entry.state === 'dead') ? 'won' : 'timeout');

        return;
    }

    if (now - raid.startedAt > RAID_TIMEOUT_MS) {
        endRaid('timeout');

        return;
    }

    const defenders = defendersOf(raid.opKey, 45).filter((player) => player.alive);

    for (const entry of alive) {
        const npc = entry.character.placed();

        if (!npc) {
            continue;
        }

        const target = defenders
            .map((player) => ({ player, gap: distance(player.position, npc.position) }))
            .sort((a, b) => a.gap - b.gap)[0]?.player;

        if (target) {
            if (entry.targetId !== target.id || !npc.getTask()) {
                entry.targetId = target.id;
                npc.attack(target);

                if (Math.random() < 0.3) {
                    npc.say(LINE.spotted);
                }
            }
        } else if (entry.targetId !== 'crates' || !npc.getTask()) {
            entry.targetId = 'crates';
            npc.walkTo(CRATES, { run: true, direct: true, radius: ARRIVE });
        }
    }

    const atCrates = alive.some((entry) => {
        const npc = entry.character.npc;

        return npc !== null && flatDistance(npc.position, CRATES) <= 6;
    });

    if (atCrates && !defenders.length) {
        raid.lostSince ||= now;

        if (now - raid.lostSince >= LOST_GRACE_MS * (raid.kind === 'revenue' ? 1 : 0.67)) {
            endRaid('lost');
        }
    } else {
        raid.lostSince = 0;
    }
}

// Members who can answer a raid: on the street of this map, not in an
// interior and not tied up in a duel, a race or the jail.
function freeMembers(opKey: string): Player[] {
    return members(opKey).filter((player) => {
        const kind = life.activity(player)?.kind;

        return onStreet(player) && !interiors.insideOf(player) && !(kind && AWAY.has(kind));
    });
}

function raidChoice(ms: number): string {
    return (
        `The revenue men are coming for your still in ${formatMinutes(ms)}. Defend it: be at the camp and fight them off. ` +
        "Or buy them off: /rum bribe at Salieri's door (F2 → Jobs). Nobody there in time and they seize every cask, case and sack stored at the camp."
    );
}

// At full heat the revenue men set out once a member is on the street to be
// warned; the clock stops while none is. They come early for a member
// already at the camp, after the first minute.
export function tickRevenue(now: number): void {
    for (const [opKey, op] of ops) {
        if (!op.unlocked || op.heat < 100) {
            raidCalls.delete(opKey);
            continue;
        }

        if (raid?.opKey === opKey) {
            continue;
        }

        const free = freeMembers(opKey);
        const pending = raidCalls.get(opKey);

        if (!pending) {
            if (!free.length) {
                continue;
            }

            raidCalls.set(opKey, { startedAt: now, dueAt: now + RAID_WINDOW_MS, pausedAt: 0 });
            tellOp(opKey, raidChoice(RAID_WINDOW_MS), 'bad', 12);
            feed('rum', [
                text('Rum', RUM),
                text(` · the revenue men are after ${op.name}'s still`),
            ]);

            continue;
        }

        if (!free.length) {
            pending.pausedAt ||= now;
            continue;
        }

        if (pending.pausedAt) {
            const left = Math.max(RAID_RESUME_MS, pending.dueAt - pending.pausedAt);

            pending.dueAt = now + left;
            // And half a minute at least before they come early.
            pending.startedAt = Math.max(pending.startedAt, now - RAID_PREP_MS / 2);
            pending.pausedAt = 0;
            tellOp(opKey, raidChoice(left), 'bad', 12);
            continue;
        }

        // One raid at a time: this one waits for the other to end.
        if (raid) {
            continue;
        }

        const defenders = defendersOf(opKey, RAID_DEFEND);
        const early = defenders.length && now - pending.startedAt >= RAID_PREP_MS;

        if (!early && now < pending.dueAt) {
            continue;
        }

        raidCalls.delete(opKey);

        if (!defenders.length) {
            // Nobody came: they take what is stored at the camp.
            raid = { kind: 'revenue', opKey, npcs: [], startedAt: now, lostSince: now };
            endRaid('lost');
            continue;
        }

        if (startRaid('revenue', opKey)) {
            continue;
        }

        // No simulation host to send them: they see armed men and call it in.
        op.heat = 70;
        changed();

        for (const player of defenders) {
            setWanted(player, 2);
        }

        tellOp(
            opKey,
            'The revenue men saw you were armed and called the police instead. Run!',
            'bad',
        );
    }
}

/* Sergeant Doyle */

function doyleLost(character: Character, loss: Loss): void {
    if (doyle?.character !== character) {
        return;
    }

    doyle = null;

    // Shooting a policeman is worse than not paying him.
    if (loss.died) {
        const { killer } = loss;
        const shooter = killer instanceof Player ? playerById(killer.id) : null;

        if (shooter && onStreet(shooter)) {
            setWanted(shooter, 2);
        }
    }
}

function startDoyle(opKey: string, op: Operation, target: Player): void {
    if (doyle || raid || !npcsAvailable()) {
        return;
    }

    const point = pick(RAID_POINTS);
    const character: Character = new Character({
        onLost: (_npc, loss) => doyleLost(character, loss),
    });

    if (
        !character.spawn(DOYLE.model, point, headingFrom(point, POT), {
            behaviour: 'neutral',
            voice: DOYLE.voice,
            virtualWorld: MAIN_WORLD,
        })
    ) {
        return;
    }

    doyle = {
        opKey,
        character,
        targetId: target.id,
        price: Math.max(DOYLE_MIN, Math.round((op.heat * DOYLE_PER_HEAT) / 10) * 10),
        startedAt: Date.now(),
        walkAt: 0,
        metAt: 0,
    };

    cooldownOf(opKey).doyleAt = Date.now();
    tellOp(
        opKey,
        `Sergeant Doyle of the county police is walking up to the camp. He'll want ${formatMoney(doyle.price)} to forget what he saw: ` +
            'press G next to him to pay.',
        'gold',
        9,
    );
}

function endDoyle(paid: boolean): void {
    const current = doyle;

    if (!current) {
        return;
    }

    doyle = null;
    const op = ops.get(current.opKey);
    const npc = current.character.npc;

    if (paid) {
        if (op) {
            op.heat = Math.max(0, op.heat - 50);
        }

        changed();

        if (npc?.alive) {
            npc.say(LINE.shrug);
        }
    } else if (op) {
        addHeat(current.opKey, HEAT_REFUSED);
        const target = playerById(current.targetId);

        if (target && onStreet(target)) {
            setWanted(target, 1);
            toast(
                target,
                "Doyle wasn't paid, so he's reporting you. The police are on their way.",
                {
                    title: TITLE,
                    tone: 'bad',
                },
            );
        }
    }

    current.character.walkOff(RAID_POINTS[0], DOYLE_LEAVE_MS);
}

export function tickDoyle(now: number): void {
    if (!doyle) {
        return;
    }

    let target = playerById(doyle.targetId);

    if (!target || !onStreet(target) || flatDistance(target.position, POT) > CAMP_RADIUS * 1.5) {
        target = defendersOf(doyle.opKey, CAMP_RADIUS)[0] ?? null;

        if (target) {
            doyle.targetId = target.id;
        }
    }

    if (
        !target ||
        now - doyle.startedAt > DOYLE_WAIT_MS * 2 ||
        (doyle.metAt && now - doyle.metAt > DOYLE_WAIT_MS)
    ) {
        endDoyle(false);

        return;
    }

    const npc = doyle.character.placed();

    if (!npc) {
        return;
    }

    const gap = flatDistance(target.position, npc.position);

    if (gap > ARRIVE + 1 && now >= doyle.walkAt) {
        doyle.walkAt = now + 3000;
        npc.walkTo(target.position, { direct: true, radius: ARRIVE });
    } else if (gap <= ARRIVE + 1 && !doyle.metAt) {
        doyle.metAt = now;
        npc.face(target.position);
        speak(
            target,
            'Sgt. Doyle',
            `Smells like Havana out here. ${formatMoney(doyle.price)} and I never walked up this path.`,
            { npc, voice: LINE.halt },
        );
    }
}

/** Doyle's price when he stands within reach of a player of his operation. */
export function doyleWaitingFor(player: Player): number | null {
    const npc = doyle?.character.placed();

    if (
        !doyle ||
        !npc ||
        doyle.opKey !== opKeyOf(player) ||
        flatDistance(player.position, npc.position) > ARRIVE + 1.5
    ) {
        return null;
    }

    return doyle.price;
}

/** G next to Doyle. */
export function payDoyle(player: Player): void {
    if (!doyle) {
        return;
    }

    if (!player.trySpendMoney(doyle.price)) {
        toast(player, `Doyle wants ${formatMoney(doyle.price)}. You don't have it.`, {
            title: TITLE,
            tone: 'bad',
        });

        return;
    }

    speak(player, 'Sgt. Doyle', 'Pleasure doing business. I was never here.', {
        npc: doyle.character.npc,
        voice: LINE.shrug,
        seconds: 5,
    });

    endDoyle(true);
}

/** The roll for a visit to an operation with members at the camp. */
export function rollVisit(
    opKey: string,
    op: Operation,
    atCamp: readonly Player[],
    wait: { raidAt: number; doyleAt: number },
    now: number,
): void {
    if (!atCamp.length || raid || doyle) {
        return;
    }

    if (
        op.cases.n >= RIVAL_MIN_CASES &&
        now - wait.raidAt > OP_EVENT_GAP_MS &&
        Math.random() < RIVAL_CHANCE
    ) {
        startRaid('rivals', opKey);
    } else if (
        op.heat >= HEAT_DOYLE &&
        op.heat < 100 &&
        now - wait.doyleAt > OP_EVENT_GAP_MS &&
        Math.random() < DOYLE_CHANCE
    ) {
        startDoyle(opKey, op, atCamp[0]);
    }
}

/** The resource stops: everyone leaves the camp. */
export function dismissVisitors(): void {
    if (raid) {
        for (const entry of raid.npcs) {
            if (entry.state === 'alive') {
                entry.character.remove();
            }
        }

        raid = null;
    }

    if (doyle?.character.npc?.alive) {
        doyle.character.remove();
    }

    doyle = null;
}

/** The mission change took them; the revenue men set out afresh, with a fresh warning, once the map is back. */
export function forgetVisitors(): void {
    raidCalls.clear();
    raid = null;
    doyle = null;
}
