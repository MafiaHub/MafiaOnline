// The players' quest progress and the operations that run the still: one per
// crew, or per crewless player. Stock, casks, heat and quest progress are
// kept in the database; casks age and heat cools on the server clock while
// this map is loaded.
import * as profiles from '../lib/profiles';
import * as store from '../lib/store';
import type { CrewTag } from '../lib/nametag';
import { toast, type Tone } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { inFreeRide, plural } from '../lib/util';
import { moveLoads } from './cargo';
import { stillBatch } from './still';

export const MAP_ID = 'freeride_extended';
export const TITLE = 'Rum distillery';
export const RUM = '#d08a3c';

const STORE = 'distillery';
const SAVE_MS = 15_000;
const NOTICE_MS = 30_000;

export const MAX_CASKS = 4;
export const MAX_CASES = 24;
export const MAX_MOLASSES = 6;

export const QUEST = { ASK: 0, FIND: 1, COPPER: 2, FIRST_FIRE: 3, FIRST_SALE: 4, DONE: 5 } as const;

export const QUESTS: readonly { title: string; step: string }[] = [
    {
        title: "A sailor's tale",
        step: "Ask Ernesto, the sailor in Salieri's billiard room, about rum",
    },
    {
        title: "The woodcutters' clearing",
        step: "Find Hank's camp in the woods west of the lowland road",
    },
    {
        title: 'Copper',
        step: 'Unbolt the copper worm off the hardware truck in the village and bring it to Hank',
    },
    { title: 'First fire', step: 'Fetch molasses, run the still and bottle your first rum' },
    {
        title: "Luigi's thirst",
        step: 'Load the cases into a car in the clearing and sell them in town',
    },
];

export const XP = {
    lead: 40,
    find: 60,
    copper: 120,
    batch: 20,
    bottle: 8,
    sell: 6,
    defend: 80,
    wedding: 60,
} as const;

/** Cases of rum: how many, and their qualities (0-100) summed. */
export interface Stock {
    n: number;
    q: number;
}

export interface Cask {
    /** Quality, 0-100. */
    q: number;
    /** Ageing left, in ms; ready at 0 or below. */
    left: number;
}

export interface Operation {
    /** "[TAG]" for a crew, else the player's nickname. */
    name: string;
    /** Somebody in it brought Hank his copper: it runs the still. */
    unlocked: boolean;
    progress: number;
    molasses: number;
    casks: Cask[];
    cases: Stock;
    heat: number;
    sold: number;
}

/**
 * A player's own part of the story: copper once they brought Hank his worm
 * (paid once, and any operation they are in runs the still), done once an
 * operation of theirs made its first sale.
 */
interface QuestRecord {
    name: string;
    stage: number;
    copper: boolean;
    done: boolean;
}

/** Cooldowns of an operation's events; not saved. */
interface Cooldowns {
    doyleAt: number;
    raidAt: number;
    theftAt: number;
    toldAt: number;
    weddingAt: number;
    bribeAt: number;
}

interface Saved {
    players?: Record<string, Partial<QuestRecord>>;
    ops?: Record<string, Partial<Operation> & { raidDueAt?: unknown }>;
}

// player record key -> their quest progress
const players = new Map<string, QuestRecord>();

// operation key ("crew:TAG" or "p:<record key>") -> its stock, heat and progress
export const ops = new Map<string, Operation>();
const cooldowns = new Map<string, Cooldowns>();
// Toasts that would repeat every tick (a buyer refusing a car with the police
// on it), at most once in NOTICE_MS per kind.
const notices = new PlayerMap<Map<string, number>>();
let dirty = false;
let lastSaveAt = 0;

export function available(): boolean {
    return inFreeRide() && World.getMission() === MAP_ID && typeof MapObject !== 'undefined';
}

function freshOp(opName: string): Operation {
    return {
        name: opName,
        unlocked: false,
        progress: QUEST.FIRST_FIRE,
        molasses: 0,
        casks: [],
        cases: { n: 0, q: 0 },
        heat: 0,
        sold: 0,
    };
}

export function load(): void {
    const saved = store.load<Saved>(STORE) ?? {};

    for (const [key, record] of Object.entries(saved.players ?? {})) {
        players.set(key, {
            name: record.name ?? '',
            stage: Math.max(0, Math.min(QUEST.COPPER, Number(record.stage) || 0)),
            copper: Boolean(record.copper),
            done: Boolean(record.done),
        });
    }

    for (const [key, op] of Object.entries(saved.ops ?? {})) {
        const { raidDueAt: _raidDueAt, ...kept } = op;

        ops.set(key, { ...freshOp(op.name ?? ''), ...kept });
    }
}

export function persist(): void {
    const kept = [...ops].filter(
        ([, op]) => op.unlocked || op.molasses || op.casks.length || op.cases.n || op.heat,
    );
    const started = [...players].filter(([, record]) => record.stage > QUEST.ASK || record.copper);

    store.save(STORE, { players: Object.fromEntries(started), ops: Object.fromEntries(kept) });
    dirty = false;
    lastSaveAt = Date.now();
}

export function changed(): void {
    dirty = true;
}

export function saveIfDue(now: number): void {
    if (dirty && now - lastSaveAt >= SAVE_MS) {
        persist();
    }
}

function crewOf(player: Player): CrewTag | null {
    return player.state.get('fr.crew') ?? null;
}

export function recordOf(player: Player): QuestRecord | null {
    const key = profiles.keyOf(player);

    if (!key) {
        return null;
    }

    let record = players.get(key);

    if (!record) {
        record = { name: player.nickname, stage: QUEST.ASK, copper: false, done: false };
        players.set(key, record);
    }

    record.name = player.nickname;

    return record;
}

function soloKeyOf(player: Player): string | null {
    const key = profiles.keyOf(player);

    return key ? `p:${key}` : null;
}

export function opKeyOf(player: Player): string | null {
    const key = profiles.keyOf(player);

    if (!key) {
        return null;
    }

    const tag = crewOf(player)?.tag;

    return tag ? `crew:${tag}` : `p:${key}`;
}

export function quality(entry: Stock): number {
    return entry.n ? Math.round(entry.q / entry.n) : 0;
}

/** Takes `count` cases of the stock's average quality out of it. */
export function takeCases(stock: Stock, count: number): Stock {
    const each = quality(stock);

    stock.n -= count;
    stock.q -= count * each;

    if (!stock.n) {
        stock.q = 0;
    }

    return { n: count, q: count * each };
}

export function addCases(stock: Stock, cases: Stock): void {
    stock.n += cases.n;
    stock.q += cases.q;
}

// A player who joins a crew brings their own operation into the crew's: its
// loads on the road, its progress and as much of its stock as the crew's
// shed has room for (the rest stays in their own, theirs again if they
// leave). Its heat comes along: hot rum does not cool by changing hands.
function mergeSolo(player: Player, opKey: string, op: Operation): void {
    const soloKey = soloKeyOf(player);
    const solo = soloKey ? ops.get(soloKey) : null;

    if (!soloKey || !solo?.unlocked) {
        return;
    }

    moveLoads(soloKey, opKey, op.name);

    // A batch of theirs on the still finishes into their own casks first.
    if (stillBatch()?.opKey === soloKey) {
        return;
    }

    const sacks = Math.max(0, Math.min(solo.molasses, MAX_MOLASSES - op.molasses));
    const room = Math.max(0, Math.min(solo.casks.length, MAX_CASKS - op.casks.length));
    const cases = Math.max(0, Math.min(solo.cases.n, MAX_CASES - op.cases.n));

    if (op.unlocked && !sacks && !room && !cases) {
        return;
    }

    op.progress = op.unlocked ? Math.max(op.progress, solo.progress) : solo.progress;
    op.unlocked = true;
    op.molasses += sacks;
    solo.molasses -= sacks;
    const casks = solo.casks.splice(0, room);

    op.casks.push(...casks);

    if (cases) {
        addCases(op.cases, takeCases(solo.cases, cases));
    }

    op.heat = Math.max(op.heat, solo.heat);
    dirty = true;
    const brought = [
        sacks ? plural(sacks, 'sack') : null,
        casks.length ? plural(casks.length, 'cask') : null,
        cases ? plural(cases, 'case') : null,
    ].filter(Boolean);
    const left = solo.molasses || solo.casks.length || solo.cases.n;

    tellOp(
        opKey,
        `${player.nickname} brought their rum into the crew's still${brought.length ? `: ${brought.join(', ')}` : ''}.` +
            (left ? ' The rest stays in their own stock until there is room.' : ''),
        'gold',
        8,
    );
}

/** The player's operation, made on first use; null before they have a player record. */
export function opOf(player: Player): Operation | null {
    const opKey = opKeyOf(player);

    if (!opKey) {
        return null;
    }

    let op = ops.get(opKey);
    const crew = crewOf(player);
    const opName = crew?.tag ? `[${crew.tag}]` : player.nickname;

    if (!op) {
        op = freshOp(opName);
        ops.set(opKey, op);
    }

    op.name = opName;

    if (crew?.tag) {
        mergeSolo(player, opKey, op);
    }

    // Whoever once brought Hank his copper runs the still in any operation
    // they are in; the story is not told twice.
    const record = recordOf(player);

    if (record && !op.unlocked && record.copper) {
        op.unlocked = true;
        op.progress = record.done ? QUEST.DONE : QUEST.FIRST_FIRE;
        dirty = true;
    }

    if (record && op.unlocked && op.progress >= QUEST.DONE && !record.done) {
        record.done = true;
        dirty = true;
    }

    return op;
}

export function members(opKey: string | null): Player[] {
    return World.getPlayers().filter((player) => opKeyOf(player) === opKey);
}

export function stageOf(player: Player): number {
    const op = opOf(player);

    if (op?.unlocked) {
        return Math.max(QUEST.FIRST_FIRE, op.progress);
    }

    return recordOf(player)?.stage ?? QUEST.ASK;
}

/** Whether the player knows where the still is: found it, or their crew runs it. */
export function knows(player: Player): boolean {
    return stageOf(player) >= QUEST.COPPER;
}

export function cooldownOf(opKey: string): Cooldowns {
    let entry = cooldowns.get(opKey);

    if (!entry) {
        entry = { doyleAt: 0, raidAt: 0, theftAt: 0, toldAt: 0, weddingAt: 0, bribeAt: 0 };
        cooldowns.set(opKey, entry);
    }

    return entry;
}

export function wantedLevel(player: Player): number {
    return typeof Police !== 'undefined' ? (Police.getRecord(player)?.level ?? 0) : 0;
}

export function setWanted(player: Player, level: number): void {
    if (typeof Police !== 'undefined' && wantedLevel(player) < level) {
        Police.setWanted(player, level);
    }
}

export function notice(player: Player, kind: string, message: string, tone: Tone = 'gold'): void {
    let sent = notices.get(player.id);

    if (!sent) {
        sent = new Map();
        notices.set(player.id, sent);
    }

    if (Date.now() - (sent.get(kind) ?? 0) < NOTICE_MS) {
        return;
    }

    sent.set(kind, Date.now());
    toast(player, message, { title: TITLE, tone, seconds: 6 });
}

export function tellOp(
    opKey: string | null,
    message: string,
    tone: Tone = 'gold',
    seconds = 7,
): void {
    for (const player of members(opKey)) {
        toast(player, message, { title: TITLE, tone, seconds });
    }
}
