// Jail. An arrest sends the player to a cell in the county jail
// (lib/prison.ts) for a sentence that grows with the wanted level they were
// caught at; the police keep their weapons. A guard walks a prisoner who gets
// out of the cell straight back. When the time is served or bail is paid,
// the warden (lib/warden.ts) unlocks the cell door, it slides open, and the
// prisoner walks down the corridor to the door at the end, which leads back
// to the city. The sentence survives a reconnect.
import * as profiles from '../lib/profiles';
import * as life from '../lib/life';
import * as log from '../lib/log';
import * as menu from '../lib/menu';
import * as commands from '../lib/commands';
import * as prison from '../lib/prison';
import * as warden from '../lib/warden';
import * as interiors from '../interiors';
import { events, every, onStart, onStop } from '../lib/lifecycle';
import { toast, card, feed, markers, name, text } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { Transfers } from '../lib/transfer';
import {
    MAIN_WORLD,
    connected,
    emit,
    formatMinutes,
    formatMoney,
    playerById,
    type Placement,
    type Point,
} from '../lib/util';
import { exitArrow, street, unarmed } from './common';

const BASE_SENTENCE_MS = 45_000;
const PER_LEVEL_MS = 30_000;
const BAIL_PER_SECOND = 5;
const TICK_MS = 1000;
const GUARD_MS = 250;
// How far past the bars a locked-up prisoner may lean before the guard acts.
const CELL_MARGIN = 0.4;
// A freed prisoner this far into the corridor has left the cell, and its
// door closes behind them.
const OUT_OF_CELL = 0.9;
const EXIT_REACH = 1.4;
// Further than this out of the hall, a freed prisoner has left it.
const HALL_MARGIN = 2;
// The door stays open at least this long, and a freed prisoner who has not
// walked out after the longest wait is shown out.
const DOOR_MIN_OPEN_MS = 4000;
const WALK_OUT_MS = 120_000;
const SPAWN_RETRY_MS = 2000;

type Stage = 'locked' | 'unlocking' | 'free';

interface Prisoner {
    until: number;
    cell: prison.Cell;
    stage: Stage;
    freedAt: number;
}

const prisoners = new PlayerMap<Prisoner>();
// A refused move fades back in as slowly as an arrival: the prisoner never
// saw the street go.
const moves = new Transfers(0.6);

export function jailed(player: Player): boolean {
    return prisoners.has(player.id);
}

function releasePoint(player: Player): Placement | null {
    return street() ?? life.spotFor(player);
}

function where({ x, y, z }: Point): string {
    return `${x.toFixed(1)} ${y.toFixed(1)} ${z.toFixed(1)}`;
}

// The first empty cell, or the one with the fewest prisoners.
function assignCell(): prison.Cell {
    const count = (cell: prison.Cell): number =>
        [...prisoners.values()].filter((prisoner) => prisoner.cell === cell).length;

    return prison.CELLS.reduce(
        (best, cell) => (count(cell) < count(best) ? cell : best),
        prison.CELLS[0],
    );
}

function lockUp(player: Player, until: number): boolean {
    if (!prison.isBuilt()) {
        return false;
    }

    if (player.getVehicle()) {
        player.removeFromVehicle();
    }

    life.end(player);
    life.begin(player, 'jail', 'In jail');
    interiors.release(player);
    const cell = prisoners.get(player.id)?.cell ?? assignCell();

    prisoners.set(player.id, { until, cell, stage: 'locked', freedAt: 0 });
    profiles.setField(player, 'jailUntil', until);
    // The police kept the guns and settled the record: "where you left off"
    // gives neither back once the sentence is over.
    const resume = profiles.get(player)?.resume;

    if (resume) {
        profiles.setField(player, 'resume', { ...resume, weapons: [], selected: 0, wanted: 0 });
    }

    markers(player, 'jail', []);
    const spawn = prison.spawnOf(cell);

    log.info(
        `${player.nickname} locked in cell ${cell.index + 1} at ${where(spawn.position)} for ${Math.round((until - Date.now()) / 1000)} s`,
    );

    moves.move(player, spawn, prison.WORLD, {
        loadout: unarmed(player),
        // The game's own countdown watch runs on the prisoner's client.
        then: () =>
            emit(player, 'fr:jail', {
                active: true,
                seconds: Math.ceil((until - Date.now()) / 1000),
            }),
    });

    return true;
}

// The time is up or bail is paid: the warden comes to open the cell.
function unlock(player: Player, message: string): void {
    const prisoner = prisoners.get(player.id);

    if (!prisoner || prisoner.stage !== 'locked') {
        return;
    }

    prisoner.stage = 'unlocking';
    log.info(`${player.nickname}'s cell ${prisoner.cell.index + 1} is being unlocked`);
    profiles.setField(player, 'jailUntil', 0);
    toast(player, message, { title: 'Released', tone: 'good' });
    warden.unlock(prisoner.cell, () => {
        log.info(`Cell ${prisoner.cell.index + 1} is open`);
        prison.openDoor(prisoner.cell);

        for (const [id, other] of prisoners) {
            if (other.cell !== prisoner.cell || other.stage !== 'unlocking') {
                continue;
            }

            other.stage = 'free';
            other.freedAt = Date.now();
            const freed = playerById(id);

            if (!freed) {
                continue;
            }

            emit(freed, 'fr:jail', { active: false });
            markers(freed, 'jail', [exitArrow()]);
            toast(freed, 'Your cell is open. The way out is the door at the end of the corridor.', {
                seconds: 6,
            });
        }
    });
}

// Out the jail's door and back to the city.
function walkOut(player: Player): void {
    const spot = releasePoint(player);

    if (!prisoners.has(player.id) || moves.isMoving(player) || !spot) {
        return;
    }

    moves.move(player, spot, MAIN_WORLD, {
        loadout: { ...life.STARTER_KIT, health: 100 },
        then: (moved) => {
            // Still in the hall: the guard tries again.
            if (!moved) {
                return;
            }

            prisoners.delete(player.id);
            card(player, 'jail', null);
            markers(player, 'jail', []);
            life.end(player, 'jail');
            toast(player, 'Keep your nose clean.', { title: 'Released', tone: 'good' });
        },
    });
}

/** Called by police.ts for an arrest (not a fine). */
export function sentence(player: Player, level: number): boolean {
    const ms = BASE_SENTENCE_MS + PER_LEVEL_MS * Math.max(0, level - 1);

    if (!lockUp(player, Date.now() + ms)) {
        return false;
    }

    toast(
        player,
        `${formatMinutes(ms)} in the county jail. The police kept your guns. /bail pays your way out.`,
        { title: 'Arrested', tone: 'bad', seconds: 8 },
    );

    feed('police', [name(player), text(' was locked up in the county jail')]);

    return true;
}

// Never below zero: the tick may unlock up to a second late.
function bail(prisoner: Prisoner): number {
    return Math.max(0, Math.ceil((prisoner.until - Date.now()) / 1000) * BAIL_PER_SECOND);
}

function cardRows(prisoner: Prisoner, now: number): [string, string][] {
    switch (prisoner.stage) {
        case 'locked':
            return [
                ['Released in', formatMinutes(prisoner.until - now)],
                ['Bail', formatMoney(bail(prisoner))],
            ];
        case 'unlocking':
            return [['Cell', 'The warden is coming']];
        case 'free':
            return [['Cell', 'Open: walk out']];
    }
}

every(TICK_MS, () => {
    const now = Date.now();

    warden.need('prisoners', prisoners.size > 0);

    for (const [id, prisoner] of prisoners) {
        const player = playerById(id);

        if (!player) {
            prisoners.delete(id);
            continue;
        }

        if (prisoner.stage === 'locked' && now >= prisoner.until) {
            unlock(player, 'You served your time. The warden is coming to open your cell.');
        }

        if (prisoner.stage === 'free' && now - prisoner.freedAt >= WALK_OUT_MS) {
            walkOut(player);
            continue;
        }

        card(player, 'jail', {
            title: 'In jail',
            subtitle: 'the county jail',
            rows: cardRows(prisoner, now),
            tone: prisoner.stage === 'free' ? 'good' : 'bad',
        });
    }
});

// The guard: a prisoner still locked up who is out of the cell is put
// straight back. A freed one walks out through the door at the end of the
// corridor; one who somehow leaves the hall is shown out.
function guard(player: Player, prisoner: Prisoner): void {
    const { position } = player;

    if (prisoner.stage === 'free') {
        const atExit = prison.atExit(position, EXIT_REACH);

        if (atExit || !prison.inHall(position, HALL_MARGIN)) {
            log.info(
                `${player.nickname} walked out (${atExit ? 'at the exit' : 'left the hall'}) at ${where(position)}`,
            );

            walkOut(player);
        }

        return;
    }

    if (
        player.virtualWorld === prison.WORLD &&
        prison.inCell(prisoner.cell, position, CELL_MARGIN)
    ) {
        return;
    }

    if (player.getVehicle()) {
        player.removeFromVehicle();
    }

    player.setVirtualWorld(prison.WORLD);
    const { position: cellSpot, heading } = prison.spawnOf(prisoner.cell);

    life.teleport(player, cellSpot, heading, unarmed(player));
    toast(player, 'The guard walks you back to your cell.', { tone: 'bad', seconds: 3 });
}

// An open door closes once no freed prisoner is left in its cell.
function closeEmptyCells(): void {
    for (const cell of prison.CELLS) {
        if (!prison.doorOpen(cell) || prison.doorOpenFor(cell) < DOOR_MIN_OPEN_MS) {
            continue;
        }

        const lingering = [...prisoners].some(([id, prisoner]) => {
            const player = playerById(id);

            return (
                prisoner.cell === cell &&
                prisoner.stage === 'free' &&
                player &&
                prison.inCell(cell, player.position, OUT_OF_CELL)
            );
        });

        if (!lingering) {
            prison.closeDoor(cell);
        }
    }
}

every(GUARD_MS, () => {
    for (const [id, prisoner] of prisoners) {
        const player = playerById(id);

        if (player?.alive && !moves.isMoving(player)) {
            guard(player, prisoner);
        }
    }

    closeEmptyCells();
});

commands.register(
    { name: 'bail', usage: '/bail', help: 'Pay your way out of jail.', category: 'Combat' },
    (player) => {
        const prisoner = prisoners.get(player.id);

        if (!prisoner) {
            toast(player, 'You are not in jail.');

            return;
        }

        if (prisoner.stage !== 'locked') {
            toast(player, 'You are free to go: walk out the door at the end of the corridor.');

            return;
        }

        const cost = bail(prisoner);

        if (!player.trySpendMoney(cost)) {
            toast(player, `Bail is ${formatMoney(cost)}; you have ${formatMoney(player.money)}.`, {
                tone: 'bad',
            });

            return;
        }

        unlock(player, `Bail paid: ${formatMoney(cost)}. The warden is coming to open your cell.`);
    },
);

menu.provide('jail', (player) => {
    const prisoner = prisoners.get(player.id);

    if (!prisoner) {
        return null;
    }

    if (prisoner.stage !== 'locked') {
        return { secondsLeft: 0, bail: 0, stage: prisoner.stage };
    }

    return {
        secondsLeft: Math.ceil((prisoner.until - Date.now()) / 1000),
        bail: bail(prisoner),
        stage: prisoner.stage,
    };
});

// A player who left mid-sentence finishes it when they come back, as does
// one in the city when this resource starts again; the jail may still be
// going up after a mission change.
function resumeSentence(player: Player): void {
    if (prisoners.has(player.id)) {
        return;
    }

    const until = profiles.get(player)?.jailUntil ?? 0;

    if (until <= Date.now()) {
        return;
    }

    const { spawnGeneration } = player;
    const tryLockUp = (): void => {
        if (
            !connected(player) ||
            !player.alive ||
            player.spawnGeneration !== spawnGeneration ||
            prisoners.has(player.id)
        ) {
            return;
        }

        if (!lockUp(player, until) && prison.available()) {
            setTimeout(tryLockUp, SPAWN_RETRY_MS);
        }
    };

    setTimeout(tryLockUp, 0);
}

events.on('playerSpawn', (player) => {
    if (!life.isTransit(player)) {
        resumeSentence(player);
    }
});

// A prisoner who dies comes back to the cell, or, once freed, to the city.
events.on('playerRespawn', (player) => {
    const prisoner = prisoners.get(player.id);

    // Dead when this resource started: the sentence carries on now.
    if (!prisoner) {
        if (!life.isTransit(player)) {
            resumeSentence(player);
        }

        return;
    }

    setTimeout(() => {
        if (!prisoners.has(player.id)) {
            return;
        }

        if (prisoner.stage === 'locked') {
            lockUp(player, prisoner.until);
        } else {
            walkOut(player);
        }
    }, 0);
});

// The mission change took the jail down. Whoever was inside leaves its world
// and the activity; a sentence still running starts again at the next spawn.
events.on('missionChange', () => {
    for (const id of prisoners.keys()) {
        const player = playerById(id);

        if (!player) {
            continue;
        }

        life.end(player, 'jail');

        if (player.virtualWorld === prison.WORLD) {
            player.setVirtualWorld(MAIN_WORLD);
        }

        card(player, 'jail', null);
        markers(player, 'jail', []);
    }

    prisoners.clear();
    moves.clear();
    warden.forget();
    prison.forget();
    prison.build();
});

onStart(() => {
    prison.build();
    warden.start();

    for (const player of World.getPlayers()) {
        if (player.alive) {
            resumeSentence(player);
        }
    }
});

onStop(() => {
    // Out of the jail before it goes; the sentence resumes on the next start.
    for (const player of World.getPlayers()) {
        if (!prisoners.has(player.id)) {
            continue;
        }

        player.setVirtualWorld(MAIN_WORLD);
        const spot = releasePoint(player);

        if (player.alive && spot) {
            life.teleport(player, spot.position, spot.heading, {
                ...life.STARTER_KIT,
                health: player.health,
            });
        }
    }

    warden.stop();
    prison.teardown();
});
