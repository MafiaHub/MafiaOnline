// The race itself: the countdown on the grid, every checkpoint checked
// against the car's replicated position, the standings sent to the HUD and
// the finish line.
import * as life from '../lib/life';
import { toast, card, compass, markers, announce } from '../lib/notify';
import { distance, flatDistance, playable, formatDuration, emit } from '../lib/util';
import { recordOf } from './routes';
import { racerList, leaveRace, routeMarkers, ordinal, type Race, type Racer } from './race';
import { takenAway } from './lobby';
import { concludeRace } from './results';

const STATE_EVERY_MS = 500;
const CHECKPOINT_RADIUS = 16;
const CHECKPOINT_HEIGHT = 9;
const OUT_OF_CAR_MS = 15_000;
const FINISH_GRACE_MS = 90_000;
const RACE_LIMIT_MS = 15 * 60_000;
// A car held on its grid slot is put back once it creeps further than this.
const GRID_DRIFT = 1.5;

// Finished racers by time, then the rest by checkpoint and how close they
// are to the next one.
function placeOrder(race: Race): Racer[] {
    const cps = race.route.checkpoints;

    return racerList(race).sort((a, b) => {
        if (a.finishedMs !== null || b.finishedMs !== null) {
            return (a.finishedMs ?? Infinity) - (b.finishedMs ?? Infinity);
        }

        if (a.cp !== b.cp) {
            return b.cp - a.cp;
        }

        const carA = a.player.getVehicle();
        const carB = b.player.getVehicle();
        const nextA = carA && cps[a.cp] ? distance(carA.position, cps[a.cp]) : Infinity;
        const nextB = carB && cps[b.cp] ? distance(carB.position, cps[b.cp]) : Infinity;

        return nextA - nextB;
    });
}

function sendState(race: Race): void {
    const order = placeOrder(race);
    const elapsed = Date.now() - race.startedAt;

    order.forEach((racer, index) => {
        if (racer.dnf) {
            return;
        }

        emit(racer.player, 'fr:race:state', {
            active: true,
            route: race.route.name,
            checkpoint: Math.min(racer.cp + 1, race.route.checkpoints.length),
            total: race.route.checkpoints.length,
            place: index + 1,
            racers: order.length,
            elapsedMs: racer.finishedMs ?? elapsed,
            finished: racer.finishedMs !== null,
            record: recordOf(race.route.id),
            standings: order.slice(0, 6).map((other, place) => ({
                place: place + 1,
                name: other.player.nickname,
                you: other.player.id === racer.player.id,
                finishedMs: other.finishedMs,
            })),
        });
    });
}

function finishRacer(race: Race, racer: Racer): void {
    const finishedMs = Date.now() - race.startedAt;

    racer.finishedMs = finishedMs;

    if (!race.firstFinishAt) {
        race.firstFinishAt = Date.now();
    }

    const place = racerList(race).filter((other) => other.finishedMs !== null).length;

    markers(racer.player, 'race', []);
    compass(racer.player, 'race', 100, null);
    life.end(racer.player, 'race');
    announce(
        racer.player,
        place === 1 && race.entrants > 1 ? 'WINNER!' : `FINISHED · ${ordinal(place)}`,
        3,
        'beep50.wav',
    );

    toast(racer.player, `${ordinal(place)} place in ${formatDuration(finishedMs)}.`, {
        title: race.route.name,
        tone: 'gold',
    });
}

export function tickCountdown(race: Race, now: number): void {
    for (const racer of racerList(race)) {
        const car = racer.player.getVehicle();

        if (car && racer.slot && distance(car.position, racer.slot.position) > GRID_DRIFT) {
            car.setTransform(racer.slot.position, racer.slot.heading, { x: 0, y: 0, z: 0 });
        }
    }

    if (now >= race.startedAt) {
        race.phase = 'running';

        for (const racer of racerList(race)) {
            card(racer.player, 'race', null);
        }

        sendState(race);
    }
}

function tickRacer(race: Race, racer: Racer, now: number): void {
    const cps = race.route.checkpoints;

    if (takenAway(racer)) {
        leaveRace(race, racer, null);

        return;
    }

    if (!playable(racer.player)) {
        leaveRace(race, racer, 'You dropped out of the race.');

        return;
    }

    const car = racer.player.getSeat() === 0 ? racer.player.getVehicle() : null;

    if (!car || car.terminalState !== 0) {
        racer.outOfCarSince ??= now;

        if (now - racer.outOfCarSince > OUT_OF_CAR_MS) {
            leaveRace(race, racer, "Disqualified: you left the driver's seat.");
        }

        return;
    }

    racer.outOfCarSince = null;
    const target = cps[racer.cp];

    if (
        flatDistance(car.position, target) <= CHECKPOINT_RADIUS &&
        Math.abs(car.position.y - target.y) <= CHECKPOINT_HEIGHT
    ) {
        racer.cp++;
        emit(racer.player, 'fr:race:checkpoint', { index: racer.cp, total: cps.length });

        if (racer.cp >= cps.length) {
            finishRacer(race, racer);
        } else {
            routeMarkers(race, racer);
        }

        race.lastStateAt = 0;
    }
}

export function tickRunning(race: Race, now: number): void {
    for (const racer of racerList(race)) {
        if (racer.finishedMs === null) {
            tickRacer(race, racer, now);
        }
    }

    if (now - race.lastStateAt >= STATE_EVERY_MS) {
        race.lastStateAt = now;
        sendState(race);
    }

    const remaining = racerList(race).filter((racer) => racer.finishedMs === null);

    if (
        !race.racers.size ||
        !remaining.length ||
        (race.firstFinishAt && now - race.firstFinishAt > FINISH_GRACE_MS) ||
        now - race.startedAt > RACE_LIMIT_MS
    ) {
        concludeRace(race);
    }
}
