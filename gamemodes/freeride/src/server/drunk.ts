// Drunkenness. Every drink at a bar (server/bars) adds to a player's level,
// 0 to 100, which wears off at 15 a minute. The server keeps it and
// publishes it in steps of 5 as the "fr.drunk" state key; client/drunk.ts
// turns it into the swaying camera (the retail CAMERA_SETSWING boat swing),
// a wobble of the field of view and a warm haze. The player record keeps it
// across a reconnect, minus what wore off meanwhile.
//
// At the top the player blacks out: the screen goes dark, someone helps
// themselves to part of their cash, and they come round on the pavement
// outside the bar. A drunk driver's car lurches now and then, and a
// policeman or a squad car that sees them weave books them for it (wanted,
// minor).
import * as profiles from './lib/profiles';
import * as life from './lib/life';
import * as devmode from './lib/devmode';
import * as commands from './lib/commands';
import { events, every, onStart, onStop } from './lib/lifecycle';
import { onLogin } from './lib/login';
import { toast, card } from './lib/notify';
import { PlayerMap } from './lib/players';
import {
    MAIN_WORLD,
    distance,
    speedOf,
    emit,
    formatMoney,
    formatMinutes,
    type Placement,
    type Point,
} from './lib/util';

export const MAX_LEVEL = 100;
export const DECAY_PER_SECOND = 0.25;
const PUBLISH_STEP = 5;
const TICK_MS = 1000;
const STAGES: readonly { from: number; name: string }[] = [
    { from: 70, name: 'Wasted' },
    { from: 45, name: 'Drunk' },
    { from: 20, name: 'Tipsy' },
    { from: 1, name: 'Merry' },
];
// The blackout: the screen is dark for this long, the player wakes this
// drunk, and loses this share of their cash, within the bounds.
const BLACKOUT_DARK_MS = 2500;
const BLACKOUT_WAKE_MS = 4000;
const HANGOVER_LEVEL = 35;
const BLACKOUT_LOSS = { share: 0.15, min: 20, max: 400 } as const;
// Driving: from this level the car lurches every few seconds, by up to
// this many radians at the top; the police notice from DRUNK_DRIVING.
const LURCH_FROM = 30;
const LURCH_MIN_SPEED = 6;
const LURCH_RADIANS = { min: 0.035, max: 0.1 } as const;
const DRUNK_DRIVING = 45;
const POLICE_SIGHT = 35;
const POLICE_CHANCE = { drunk: 0.15, wasted: 0.3 } as const;
const POLICE_COOLDOWN_MS = 60_000;

const levels = new PlayerMap<number>();
const published = new PlayerMap<number>();
// When each drunk driver's car lurches next.
const lurches = new PlayerMap<number>();
// When the police last noticed each player weave.
const noticed = new PlayerMap<number>();
const blackouts = new PlayerMap<number>();
// Where a player who blacked out comes round: set by server/bars.
let wakeProvider: (player: Player) => Placement | null = () => null;

export function level(player: Player): number {
    return levels.get(player.id) ?? 0;
}

export function stageOf(value: number): string {
    return STAGES.find((stage) => value >= stage.from)?.name ?? 'Sober';
}

function persist(player: Player): void {
    const value = level(player);

    profiles.setField(
        player,
        'drunk',
        value > 0 ? { level: Math.round(value * 10) / 10, at: Date.now() } : null,
    );
}

function publish(player: Player, force = false): void {
    const value = level(player);
    const stepped =
        value <= 0 ? 0 : Math.min(MAX_LEVEL, Math.ceil(value / PUBLISH_STEP) * PUBLISH_STEP);

    if (!force && published.get(player.id) === stepped) {
        return;
    }

    published.set(player.id, stepped);
    profiles.setPublic(player, 'fr.drunk', stepped > 0 ? stepped : null);
    persist(player);
    const seconds = value / DECAY_PER_SECOND;

    card(
        player,
        'drunk',
        stepped > 0
            ? {
                  title: stageOf(value),
                  subtitle: stepped >= 70 ? 'Find a bed before you fall over' : 'It will wear off',
                  rows: [['Sober in', formatMinutes(seconds * 1000)]],
                  progress: value / MAX_LEVEL,
                  tone: stepped >= 70 ? 'bad' : 'gold',
              }
            : null,
    );
}

function set(player: Player, value: number): void {
    const clamped = Math.max(0, Math.min(MAX_LEVEL, value));

    if (clamped <= 0) {
        levels.delete(player.id);
    } else {
        levels.set(player.id, clamped);
    }

    publish(player);
}

/**
 * A drink: positive amounts make the player drunker, a coffee's negative
 * one sobers. A drink someone else bought stops at `cap`, short of the
 * blackout. Returns the new level.
 */
export function add(player: Player, amount: number, cap = MAX_LEVEL): number {
    if (blackouts.has(player.id)) {
        return level(player);
    }

    const current = level(player);

    set(player, amount > 0 ? Math.max(current, Math.min(cap, current + amount)) : current + amount);

    if (level(player) >= MAX_LEVEL) {
        blackout(player);
    }

    return level(player);
}

function blackout(player: Player): void {
    const { spawnGeneration } = player;

    blackouts.set(player.id, Date.now());
    emit(player, 'fr:drunk:blackout', { seconds: (BLACKOUT_DARK_MS + BLACKOUT_WAKE_MS) / 1000 });
    setTimeout(() => {
        if (!player.alive || player.spawnGeneration !== spawnGeneration) {
            blackouts.delete(player.id);

            return;
        }

        const loss =
            player.money <= 0
                ? 0
                : Math.min(
                      player.money,
                      Math.max(
                          BLACKOUT_LOSS.min,
                          Math.min(
                              BLACKOUT_LOSS.max,
                              Math.round(player.money * BLACKOUT_LOSS.share),
                          ),
                      ),
                  );

        if (loss > 0) {
            player.giveMoney(-loss);
        }

        // A player locked up meanwhile stays in the cell.
        const wake = life.activity(player)?.kind === 'jail' ? null : wakeProvider(player);

        if (wake) {
            player.setVirtualWorld(MAIN_WORLD);
            life.teleport(player, wake.position, wake.heading ?? 0);
        }

        setTimeout(() => {
            blackouts.delete(player.id);
            levels.set(player.id, HANGOVER_LEVEL);
            publish(player, true);
            emit(player, 'fr:drunk:wake', {});
            toast(
                player,
                loss > 0
                    ? `You come round on the pavement with a split lip. Somebody helped themselves to ${formatMoney(loss)}.`
                    : 'You come round on the pavement with a split lip and empty pockets.',
                { title: 'Blackout', tone: 'bad', seconds: 8 },
            );
        }, BLACKOUT_WAKE_MS);
    }, BLACKOUT_DARK_MS);
}

interface Rotation {
    w: number;
    x: number;
    y: number;
    z: number;
}

function headingQuaternion(angle: number): Rotation {
    return { w: Math.cos(angle / 2), x: 0, y: Math.sin(angle / 2), z: 0 };
}

function multiply(a: Rotation, b: Rotation): Rotation {
    return {
        w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

function nextLurchDelay(value: number): number {
    return value >= 70 ? 2500 + Math.random() * 2500 : 4000 + Math.random() * 4000;
}

// The car swings off its line: its heading and velocity turn by a few
// degrees where the driver's client has it by now.
function lurch(player: Player, vehicle: Vehicle, value: number): void {
    const speed = speedOf(vehicle);

    if (speed < LURCH_MIN_SPEED) {
        return;
    }

    const now = Date.now();
    const due = lurches.get(player.id);

    if (due === undefined) {
        lurches.set(player.id, now + nextLurchDelay(value));

        return;
    }

    if (now < due) {
        return;
    }

    lurches.set(player.id, now + nextLurchDelay(value));
    const share = (value - LURCH_FROM) / (MAX_LEVEL - LURCH_FROM);
    const angle =
        (LURCH_RADIANS.min + (LURCH_RADIANS.max - LURCH_RADIANS.min) * share) *
        (Math.random() < 0.5 ? -1 : 1);
    const ahead = Math.min(
        0.3,
        Math.max(0.05, (player.ping > 0 ? player.ping / 2000 : 0.05) + 0.05),
    );
    const { x, y, z } = vehicle.velocity;
    const position: Point = {
        x: vehicle.position.x + x * ahead,
        y: vehicle.position.y + y * ahead,
        z: vehicle.position.z + z * ahead,
    };
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    const velocity: Point = { x: x * cos + z * sin, y, z: -x * sin + z * cos };
    // setTransform reads w, x, y and z off any object; the API types only its Quaternion.
    const rotation = multiply(headingQuaternion(angle), vehicle.rotation) as unknown as Quaternion;

    vehicle.setTransform(position, rotation, velocity);
}

type Witness = { npc: Npc } | { car: Vehicle } | { officer: Player };

// The retail police on the host (its officers and squad cars) and the
// players on police duty.
function policeWitness(player: Player, position: Point): Witness | null {
    const npc =
        typeof Npc !== 'undefined' && typeof Npc.getInRange === 'function'
            ? Npc.getInRange(position, POLICE_SIGHT).find(
                  (candidate) => candidate.kind === 'police' && candidate.alive,
              )
            : null;

    if (npc) {
        return { npc };
    }

    const car = World.getVehicles().find(
        (vehicle) =>
            vehicle.traffic &&
            vehicle.trafficKind === 'police' &&
            vehicle.terminalState === 0 &&
            distance(vehicle.position, position) <= POLICE_SIGHT,
    );

    if (car) {
        return { car };
    }

    const officer =
        typeof Police.isOfficer === 'function'
            ? World.getPlayers().find(
                  (other) =>
                      other.id !== player.id &&
                      other.alive &&
                      Police.isOfficer(other) &&
                      other.getVehicle() !== player.getVehicle() &&
                      distance(other.position, position) <= POLICE_SIGHT,
              )
            : null;

    return officer ? { officer } : null;
}

function watchDriver(player: Player, vehicle: Vehicle, value: number): void {
    if (
        value < DRUNK_DRIVING ||
        speedOf(vehicle) < 4 ||
        (typeof Police.isOfficer === 'function' && Police.isOfficer(player))
    ) {
        return;
    }

    if ((Police.getRecord(player)?.level ?? 0) > 0) {
        return;
    }

    const now = Date.now();

    if (now - (noticed.get(player.id) ?? 0) < POLICE_COOLDOWN_MS) {
        return;
    }

    if (Math.random() >= (value >= 70 ? POLICE_CHANCE.wasted : POLICE_CHANCE.drunk)) {
        return;
    }

    const witness = policeWitness(player, vehicle.position);

    if (!witness) {
        return;
    }

    noticed.set(player.id, now);

    if (!Police.setWanted(player, 1)) {
        return;
    }

    toast(player, 'A policeman saw you weave across the road. You are wanted for drunk driving.', {
        title: 'Police',
        tone: 'bad',
    });

    if ('officer' in witness) {
        toast(witness.officer, `${player.nickname} is driving drunk. Pull them over.`, {
            title: 'Dispatch',
            tone: 'gold',
        });
    }
}

function tick(): void {
    const seconds = TICK_MS / 1000;

    for (const player of World.getPlayers()) {
        const value = level(player);

        if (value <= 0 || blackouts.has(player.id)) {
            continue;
        }

        const vehicle = player.alive ? player.getVehicle() : null;

        if (vehicle && player.getSeat() === 0) {
            if (value >= LURCH_FROM) {
                lurch(player, vehicle, value);
            }

            watchDriver(player, vehicle, value);
        } else {
            lurches.delete(player.id);
        }

        set(player, value - DECAY_PER_SECOND * seconds);
    }
}

// What wore off while the player was away.
function restore(player: Player): void {
    const saved = profiles.get(player)?.drunk;

    if (!saved || typeof saved.level !== 'number') {
        return;
    }

    const away = Math.max(0, (Date.now() - (saved.at ?? 0)) / 1000);
    const value = saved.level - away * DECAY_PER_SECOND;

    if (value > 0) {
        levels.set(player.id, Math.min(MAX_LEVEL - 1, value));
        publish(player, true);
    } else {
        profiles.setField(player, 'drunk', null);
    }
}

function forget(player: Player): void {
    levels.delete(player.id);
    published.delete(player.id);
    lurches.delete(player.id);
    noticed.delete(player.id);
    blackouts.delete(player.id);
}

commands.register(
    {
        name: 'drunk',
        usage: '/drunk [0-100]',
        help: 'Show or set how drunk you are (development mode).',
        category: 'Dev',
        hidden: true,
    },
    (player, [value]) => {
        if (value !== undefined && devmode.isOn()) {
            const wanted = Number(value);

            if (Number.isFinite(wanted)) {
                set(player, wanted);

                if (level(player) >= MAX_LEVEL) {
                    blackout(player);
                }
            }
        }

        toast(player, `${stageOf(level(player))} (${Math.round(level(player))}/100).`);
    },
);

onStart(() => {
    every(TICK_MS, tick);

    for (const player of World.getPlayers()) {
        restore(player);
    }
});

onStop(() => {
    for (const player of World.getPlayers()) {
        if (levels.has(player.id)) {
            persist(player);
        }

        profiles.setPublic(player, 'fr.drunk', null);
        card(player, 'drunk', null);
        forget(player);
    }
});

// Once selector/login.ts has attached the player record at login.
onLogin(restore);

events.on('playerDisconnect', (player) => {
    if (levels.has(player.id)) {
        persist(player);
    }
});

export function isBlackedOut(player: Player): boolean {
    return blackouts.has(player.id);
}

export function setWakeProvider(provider: (player: Player) => Placement | null): void {
    wakeProvider = provider;
}
