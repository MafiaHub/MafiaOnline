// Before the start: the lobby everyone in the city is invited to, and the
// starting grid. Street racers drive to the start line; circuit racers are
// put on the grid in identical cars, wherever they were.
import * as life from '../lib/life';
import * as log from '../lib/log';
import { toast, card, compass, markers, feed, name, text, COLORS } from '../lib/notify';
import {
    MAIN_WORLD,
    distance,
    playable,
    formatMinutes,
    emit,
    headingOf,
    plain,
    plural,
    type Placement,
} from '../lib/util';
import { spawnVehicle } from '../lib/vehicles';
import { MAX_RACERS, lengthText, type Route } from './routes';
import {
    races,
    raceOf,
    openLobby,
    racerList,
    leaveRace,
    closeRace,
    routeMarkers,
    type ConcludeHandler,
    type Race,
    type Racer,
} from './race';

const LOBBY_MS = 45_000;

export const COUNTDOWN_MS = 4_000;
export const START_REACH = 150;
const MAX_RACES = 3;
// Circuit races put every entrant on the grid in the same car, wherever they
// were, and bring them back to that spot afterwards.
const CIRCUIT_CAR = 'speedster00.i3d';

/** A race opened by a scripted event rather than a player. */
export interface RaceOptions {
    /** Names the host. */
    title?: string;
    lobbyMs?: number;
    /** Receives the finishers and the number who took the start. */
    onConclude?: ConcludeHandler;
}

let nextRaceId = 1;

function lobbyCard(race: Race, racer: Racer): void {
    const { route } = race;
    const circuit = Boolean(route.circuit);
    const car = racer.player.getVehicle();
    const seated = racer.player.getSeat() === 0;
    const atStart =
        circuit || (seated && car !== null && distance(car.position, route.start) <= START_REACH);
    const you = circuit
        ? 'You will be put on the grid'
        : atStart
          ? 'At the start line'
          : seated
            ? 'Drive to the start'
            : 'Get a car (/garage)';

    card(racer.player, 'race', {
        title: route.name,
        subtitle: circuit
            ? `Circuit · ${plural(route.laps ?? 1, 'lap')} · ${lengthText(route)}`
            : `Street race · ${lengthText(route)} · ${route.checkpoints.length} checkpoints`,
        rows: [
            ['Starts in', formatMinutes(race.lobbyEndsAt - Date.now())],
            ['Racers', String(race.racers.size)],
            ['You', you],
        ],
        progress: Math.max(0, (race.lobbyEndsAt - Date.now()) / race.lobbyMs),
        tone: atStart ? 'good' : 'gold',
    });
}

export function join(race: Race, player: Player): boolean {
    if (race.racers.has(player.id)) {
        return true;
    }

    if (race.racers.size >= MAX_RACERS) {
        toast(player, 'That race is full.', { tone: 'bad' });

        return false;
    }

    if (!life.begin(player, 'race-lobby', `Race lobby · ${race.route.name}`)) {
        toast(player, life.busyMessage(player) ?? '', { tone: 'bad' });

        return false;
    }

    race.racers.set(player.id, {
        player,
        cp: 0,
        finishedMs: null,
        dnf: false,
        outOfCarSince: null,
        slot: null,
        returnTo: null,
        raceCarId: null,
    });

    const startsIn = formatMinutes(race.lobbyEndsAt - Date.now());

    if (race.route.circuit) {
        toast(player, `You're in. A race car will be waiting for you on the grid in ${startsIn}.`, {
            title: race.route.name,
            tone: 'gold',
        });
    } else {
        compass(player, 'race', 100, { position: race.route.start });
        markers(player, 'race', [
            {
                id: 'start',
                position: race.route.start,
                label: 'START LINE',
                color: COLORS.gold,
                beacon: true,
                primary: true,
            },
        ]);

        toast(player, `You're in. Be at the start line in a car within ${startsIn}.`, {
            title: race.route.name,
            tone: 'gold',
        });
    }

    for (const other of racerList(race)) {
        if (other.player.id !== player.id) {
            toast(other.player, `${player.nickname} joined the race.`);
        }
    }

    return true;
}

// A player opens a lobby, or with host null a scripted event such as a
// championship round does: options.title names it, options.lobbyMs sets the
// wait and options.onConclude(finishers, entrants) receives the result.
export function openRace(
    host: Player | null,
    route: Route,
    options: RaceOptions = {},
): Race | null {
    const refuse = (message: string): null => {
        if (host) {
            toast(host, message, { tone: 'bad' });
        } else {
            log.warn(message);
        }

        return null;
    };

    if (host && !playable(host)) {
        return null;
    }

    if (host && raceOf(host)) {
        return refuse('You are already in a race. /race leave first.');
    }

    if (openLobby()) {
        return refuse('A race lobby is already open. Join it with /race join.');
    }

    if (races.size >= MAX_RACES) {
        return refuse('The streets are busy with races. Try again in a minute.');
    }

    const lobbyMs = options.lobbyMs ?? LOBBY_MS;
    const race: Race = {
        id: nextRaceId++,
        route,
        host: host?.nickname ?? options.title ?? 'City Hall',
        generation: World.getMissionGeneration(),
        phase: 'lobby',
        racers: new Map(),
        lobbyMs,
        lobbyEndsAt: Date.now() + lobbyMs,
        startedAt: 0,
        firstFinishAt: 0,
        lastStateAt: 0,
        entrants: 0,
        onConclude: options.onConclude ?? null,
    };

    races.set(race.id, race);

    if (host && !join(race, host)) {
        races.delete(race.id);

        return null;
    }

    for (const player of World.getPlayers()) {
        if (player.id === host?.id || !player.spawned) {
            continue;
        }

        toast(
            player,
            `${race.host} opened ${route.circuit ? 'a circuit race' : 'a street race'}: ` +
                `${route.name}, ${lengthText(route)}. Type /race join or use F2.`,
            { title: 'Race invitation', tone: 'gold', seconds: 9 },
        );
    }

    feed('race', [
        host ? name(host, COLORS.gold) : text(race.host, COLORS.gold),
        text(' opened a race on '),
        text(route.name, COLORS.white),
    ]);

    return race;
}

// Puts a circuit entrant in a fresh race car on their grid slot.
function seatOnGrid(racer: Racer, slot: Placement): boolean {
    const { player } = racer;

    racer.returnTo = { position: plain(player.position), heading: headingOf(player.rotation) };

    if (player.getVehicle()) {
        player.removeFromVehicle();
    }

    if (!life.teleport(player, slot.position, slot.heading)) {
        return false;
    }

    const car = spawnVehicle(CIRCUIT_CAR, slot.position, slot.heading, {
        controller: player,
        owner: player,
    });

    if (!car) {
        return false;
    }

    racer.raceCarId = car.id;

    return player.putInVehicle(car, 0);
}

// Who is ready when the lobby closes; the rest are sent off with a reason.
function readyRacers(race: Race): Racer[] {
    const ready: Racer[] = [];

    for (const racer of racerList(race)) {
        const { player } = racer;

        if (race.route.circuit) {
            // Moving to the grid starts a new native life; nobody shakes off the police that way.
            if (
                !playable(player) ||
                player.virtualWorld !== MAIN_WORLD ||
                (Police.getRecord(player)?.level ?? 0) > 0
            ) {
                leaveRace(
                    race,
                    racer,
                    'Only players on the street with a clean record can be put on the grid.',
                );

                continue;
            }

            ready.push(racer);
            continue;
        }

        const car = player.getSeat() === 0 ? player.getVehicle() : null;

        if (
            !playable(player) ||
            !car ||
            car.terminalState !== 0 ||
            distance(car.position, race.route.start) > START_REACH
        ) {
            leaveRace(
                race,
                racer,
                'You were not at the start line in a car. The race went without you.',
            );

            continue;
        }

        ready.push(racer);
    }

    return ready;
}

function startCountdown(race: Race): void {
    const ready = readyRacers(race);

    if (!ready.length) {
        closeRace(race);

        return;
    }

    race.phase = 'countdown';
    race.entrants = ready.length;
    race.startedAt = Date.now() + COUNTDOWN_MS;
    // The server owns the grid: each car is put on its slot, repaired, filled
    // up and held there until the lights go green. Grid places are drawn by
    // lot so joining first is no advantage.
    const order = ready
        .map((racer) => ({ racer, lot: Math.random() }))
        .sort((a, b) => a.lot - b.lot)
        .map((entry) => entry.racer);

    order.forEach((racer, index) => {
        const slot = race.route.grid[index];

        life.end(racer.player, 'race-lobby');
        life.begin(
            racer.player,
            'race',
            `${race.route.circuit ? 'Circuit race' : 'Street race'} · ${race.route.name}`,
        );

        racer.slot = slot;

        if (race.route.circuit && !seatOnGrid(racer, slot)) {
            leaveRace(race, racer, 'Your race car could not be brought to the grid.');

            return;
        }

        const car = racer.player.getVehicle();

        if (car) {
            car.setTransform(slot.position, slot.heading, { x: 0, y: 0, z: 0 });
            car.repair();
            car.state.set('fr.fullTank', true);

            if (car.dynamicsValid && car.fuelTankCapacity > 0) {
                car.setFuel(car.fuelTankCapacity);
            }
        }

        card(racer.player, 'race', {
            title: race.route.name,
            subtitle: `Grid slot ${index + 1} of ${ready.length}`,
            rows: [['Status', 'Get ready']],
            tone: 'gold',
        });

        emit(racer.player, 'fr:race:countdown', { seconds: Math.round(COUNTDOWN_MS / 1000) - 1 });
        routeMarkers(race, racer);
    });
}

// Another activity took the racer over (an arrest puts them in jail).
export function takenAway(racer: Racer): boolean {
    const kind = life.activity(racer.player)?.kind;

    return kind !== undefined && kind !== 'race' && kind !== 'race-lobby';
}

export function tickLobby(race: Race, now: number): void {
    for (const racer of racerList(race)) {
        if (takenAway(racer)) {
            leaveRace(race, racer, null);
            continue;
        }

        if (!playable(racer.player)) {
            continue;
        }

        if (now - race.lastStateAt >= 1000) {
            lobbyCard(race, racer);
        }
    }

    if (now - race.lastStateAt >= 1000) {
        race.lastStateAt = now;
    }

    if (now >= race.lobbyEndsAt) {
        startCountdown(race);
    }
}
