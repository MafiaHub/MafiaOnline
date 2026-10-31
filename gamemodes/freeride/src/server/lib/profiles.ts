// Persistent player records: cash, experience, rank and career statistics.
// A record belongs to an account (lib/accounts.ts), which names its key; the
// records kept before accounts are keyed by nickname on one machine
// (legacyKey) until their owner registers and claims them.
// Public values travel in each player's replicated state bag, which the
// scoreboard and the world labels on every client read.
import * as store from './store';
import * as log from './log';
import { toast, feed, name, text, COLORS } from './notify';
import { emit, type Point } from './util';
import { events, every, onStop } from './lifecycle';
import type { WeaponGrant } from './life';
import { Delay } from './timers';

// Changed records are written in one transaction this long after a change.
const SAVE_DELAY_MS = 2000;

export const STARTING_CASH = 800;
const PLAYTIME_XP_EVERY_MS = 5 * 60_000;
const PLAYTIME_XP = 25;

const RANKS: readonly (readonly [number, string])[] = [
    [1, 'Greenhorn'],
    [3, 'Street Runner'],
    [5, 'Associate'],
    [8, 'Soldier'],
    [12, 'Capo'],
    [16, 'Underboss'],
    [20, 'Don'],
];

/** Career counters every new record starts at zero. */
type FirstCounter =
    | 'kills'
    | 'deaths'
    | 'racesRun'
    | 'racesWon'
    | 'raceRecords'
    | 'deliveries'
    | 'bountiesClaimed'
    | 'eventsWon'
    | 'duelsWon'
    | 'exports'
    | 'fares'
    | 'arrests'
    | 'robberies'
    | 'turfCaptures';

/** Counters added in later versions: missing on older records until first bumped. */
type LaterCounter =
    | 'patients'
    | 'championships'
    | 'carsBought'
    | 'rum'
    | 'tobacco'
    | 'rescues'
    | 'firesOut'
    | 'homesBought';

export type Counter = FirstCounter | LaterCounter;

/** A car in the player's garage (server/dealership.ts). */
export interface OwnedCar {
    model: string;
    /** Vehicle.captureState() of the car when it was put away, or null for a fresh one. */
    state: string | null;
    totaled: boolean;
    bought?: number;
    /** The home whose parking spot it stands at. */
    home?: string;
}

/** Where and how the player left the city, to carry on there (server/selector/entry.ts). */
export interface Resume {
    mission: string;
    position: Point;
    heading: number;
    health: number;
    weapons: WeaponGrant[];
    selected: number;
    wanted: number;
    at: number;
}

/** How drunk the player was, and when (server/drunk.ts). */
export interface Drunkenness {
    level: number;
    at: number;
}

/** A player's saved record. */
export interface PlayerRecord
    extends Record<FirstCounter, number>, Partial<Record<LaterCounter, number>> {
    /** The nickname the player last played under. */
    name: string;
    money: number;
    xp: number;
    bestStreak: number;
    cars: OwnedCar[];
    /** Ids of the stashes found. */
    stashes: string[];
    /** Best km/h per speed trap. */
    speedBest: Record<string, number>;
    playMs: number;
    /** The character model chosen. */
    model: string | null;
    /** The usual spawn district. */
    district: string | null;
    firstSeen: number;
    lastSeen: number;
    resume?: Resume | null;
    /** A Date.now() time until which the player is in jail. */
    jailUntil?: number;
    drunk?: Drunkenness | null;
    /** The car radio's on/off; on unless false. */
    radio?: boolean;
}

/** What leaderboards rank by: a counter, money, or level (by experience). */
export type LeaderboardField = Counter | 'money' | 'level';

export interface LeaderboardRow {
    name: string;
    value: number;
    level: number;
}

export interface Progress {
    level: number;
    rank: string;
    xp: number;
    /** Experience gained within the current level, and the level's whole span. */
    levelXp: number;
    levelSpan: number;
}

interface Session {
    key: string;
    lastPlaytimeAt: number;
}

const records = new Map<string, PlayerRecord>();
// Detached explicitly (main/players.ts), after the record has been brought up to date.
const sessions = new Map<number, Session>();
const dirty = new Set<string>();
const saveDelay = new Delay();
let stopped = false;

function load(): void {
    for (const [key, record] of store.loadPlayers<PlayerRecord>()) {
        records.set(key, record);
    }

    log.info(`Loaded ${records.size} player records`);
}

function writeNow(): void {
    saveDelay.cancel();
    const entries: [string, PlayerRecord][] = [];

    for (const key of dirty) {
        const record = records.get(key);

        if (record) {
            entries.push([key, record]);
        }
    }

    store.savePlayers(entries);
    dirty.clear();
}

// Once the resource is stopping, every change is written at once: the
// modules that stop after this one still store parked cars and resume spots,
// and a restart reads the database straight away.
function saveSoon(): void {
    if (stopped) {
        writeNow();
    } else if (!saveDelay.pending) {
        saveDelay.start(SAVE_DELAY_MS, writeNow);
    }
}

// Marks a connected player's record for the next write.
function changed(player: Player): void {
    const session = sessions.get(player.id);

    if (!session) {
        return;
    }

    dirty.add(session.key);
    saveSoon();
}

/** The key a record was kept under before accounts. */
export function legacyKey(nickname: string, hardwareId: string): string {
    const lowered = nickname.toLowerCase();

    return hardwareId ? `${lowered}@${hardwareId}` : lowered;
}

export function exists(key: string): boolean {
    return records.has(key);
}

function fresh(player: Player): PlayerRecord {
    const now = Date.now();

    return {
        name: player.nickname,
        money: STARTING_CASH,
        xp: 0,
        kills: 0,
        deaths: 0,
        bestStreak: 0,
        racesRun: 0,
        racesWon: 0,
        raceRecords: 0,
        deliveries: 0,
        bountiesClaimed: 0,
        eventsWon: 0,
        duelsWon: 0,
        cars: [],
        exports: 0,
        fares: 0,
        arrests: 0,
        robberies: 0,
        turfCaptures: 0,
        stashes: [],
        speedBest: {},
        playMs: 0,
        model: null,
        district: null,
        firstSeen: now,
        lastSeen: now,
    };
}

export function levelFor(xp: number): number {
    return Math.max(1, Math.floor((1 + Math.sqrt(1 + (8 * xp) / 100)) / 2));
}

function xpForLevel(level: number): number {
    return (100 * level * (level - 1)) / 2;
}

export function rankFor(level: number): string {
    let rank = RANKS[0][1];

    for (const [minimum, title] of RANKS) {
        if (level >= minimum) {
            rank = title;
        }
    }

    return rank;
}

/** The record key of a connected player, or null before attach. */
export function keyOf(player: Player): string | null {
    return sessions.get(player.id)?.key ?? null;
}

/** Record keys whose last nickname matches, for console commands about players who are not connected. */
export function keysNamed(nickname: string): string[] {
    const wanted = String(nickname).trim().toLowerCase();

    return [...records]
        .filter(([, record]) => String(record.name).toLowerCase() === wanted)
        .map(([key]) => key);
}

/** The connected player's record, or null before they log in. */
export function get(player: Player): PlayerRecord | null {
    const session = sessions.get(player.id);

    return session ? (records.get(session.key) ?? null) : null;
}

export function progress(record: Pick<PlayerRecord, 'xp'>): Progress {
    const level = levelFor(record.xp);
    const base = xpForLevel(level);

    return {
        level,
        rank: rankFor(level),
        xp: record.xp,
        levelXp: record.xp - base,
        levelSpan: xpForLevel(level + 1) - base,
    };
}

/** Private HUD details go to the owner; public ones ride on the state bag. */
export function publish(player: Player): void {
    const record = get(player);

    if (!record) {
        return;
    }

    const { level, rank, levelXp, levelSpan } = progress(record);

    player.state.set('fr.level', level);
    player.state.set('fr.rank', rank);
    player.state.set('fr.kills', record.kills);
    player.state.set('fr.deaths', record.deaths);
    emit(player, 'fr:profile', {
        level,
        rank,
        levelXp,
        levelSpan,
        xp: record.xp,
        money: player.money,
    });
}

/** Sets a value every client sees on the player; null or undefined removes it. */
export function setPublic(player: Player, key: string, value: unknown): void {
    if (value === null || value === undefined) {
        if (player.state.has(key)) {
            player.state.remove(key);
        }
    } else if (player.state.get(key) !== value) {
        player.state.set(key, value);
    }
}

/** Gives the player the record under `key`, a fresh one if there is none. */
export function attach(player: Player, key: string): { record: PlayerRecord; returning: boolean } {
    const existing = records.get(key);
    const record = existing ?? fresh(player);

    if (!existing) {
        records.set(key, record);
    }

    record.name = player.nickname;
    record.lastSeen = Date.now();
    sessions.set(player.id, { key, lastPlaytimeAt: Date.now() });
    player.setMoney(Math.max(0, Math.min(1_000_000_000, Math.round(record.money))));
    publish(player);
    changed(player);

    return { record, returning: Boolean(existing) };
}

// The play time since the last count goes onto the record.
function countPlaytime(session: Session, record: PlayerRecord, now = Date.now()): void {
    record.playMs += now - session.lastPlaytimeAt;
    session.lastPlaytimeAt = now;
    dirty.add(session.key);
}

/** Closes the player's session and writes their record at once. */
export function detach(player: Player): void {
    const session = sessions.get(player.id);
    const record = get(player);

    if (session && record) {
        countPlaytime(session, record);
        record.lastSeen = Date.now();
    }

    sessions.delete(player.id);
    writeNow();
}

export { writeNow as save };

export function addXp(player: Player, amount: number, reason?: string): void {
    const record = get(player);

    if (!record || amount <= 0) {
        return;
    }

    const before = levelFor(record.xp);

    record.xp += Math.round(amount);
    const after = levelFor(record.xp);

    emit(player, 'fr:xp', { amount: Math.round(amount), reason: reason ?? '' });
    publish(player);
    changed(player);

    if (after > before) {
        const rank = rankFor(after);
        const promoted = rank !== rankFor(before);

        toast(player, promoted ? `You are now a ${rank}.` : `Level ${after} reached.`, {
            title: `Level ${after}`,
            tone: 'gold',
            seconds: 6,
        });

        emit(player, 'fr:levelup', { level: after, rank, promoted });
        feed('level', [
            name(player, COLORS.gold),
            text(promoted ? ` made ${rank} (level ${after})` : ` reached level ${after}`),
        ]);
    }
}

/** Adds to a career counter; one an older record lacks starts at zero. */
export function bump(player: Player, field: Counter, amount = 1): void {
    const record = get(player);

    if (!record) {
        return;
    }

    // A record edited by hand may hold anything.
    const current: unknown = record[field];

    if (current !== undefined && typeof current !== 'number') {
        return;
    }

    record[field] = (current ?? 0) + amount;
    changed(player);
}

export function setField<K extends keyof PlayerRecord>(
    player: Player,
    field: K,
    value: PlayerRecord[K],
): void {
    const record = get(player);

    if (!record) {
        return;
    }

    record[field] = value;
    changed(player);
}

export function leaderboard(field: LeaderboardField, count = 8): LeaderboardRow[] {
    const rows = [...records.values()].map((record) => ({
        name: record.name,
        value: field === 'level' ? record.xp : (record[field] ?? 0),
        level: levelFor(record.xp),
    }));

    return rows
        .filter((row) => row.value > 0)
        .sort((a, b) => b.value - a.value)
        .slice(0, count);
}

function tickPlaytime(): void {
    const now = Date.now();

    for (const player of World.getPlayers()) {
        const session = sessions.get(player.id);
        const record = get(player);

        if (!session || !record || now - session.lastPlaytimeAt < PLAYTIME_XP_EVERY_MS) {
            continue;
        }

        countPlaytime(session, record, now);
        saveSoon();

        if (player.spawned) {
            addXp(player, PLAYTIME_XP, 'Time on the streets');
        }
    }
}

events.on('playerMoneyChange', (player, _old, money) => {
    const record = get(player);

    if (!record) {
        return;
    }

    record.money = money;
    emit(player, 'fr:profile', { ...progress(record), money });
    changed(player);
});

every(30_000, tickPlaytime);

onStop(() => {
    for (const player of World.getPlayers()) {
        const session = sessions.get(player.id);
        const record = get(player);

        if (session && record) {
            countPlaytime(session, record);
        }
    }

    stopped = true;
    writeNow();
});

// The HUD view exists only once the client has loaded the resource.
events.on('playerSpawn', publish);

load();
