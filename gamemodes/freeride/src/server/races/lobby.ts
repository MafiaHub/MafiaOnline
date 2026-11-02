// Before the start: the lobby everyone in the city is invited to, and the
// starting grid. Street racers drive to the start line, which the compass,
// the city map and a red smoke flare point them to; once entries close the
// race waits there until every entrant has arrived, or GATHER_MS at most.
// Circuit racers are put on the grid in identical cars, wherever they were.
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
// After entries close, the longest a street race waits for its racers at the
// start line; those still missing then are left behind.
const GATHER_MS = 3 * 60_000;

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

// A circuit puts everyone on the grid; a street racer is ready in the
// driver's seat of a working car at the start line.
function atStartLine(race: Race, racer: Racer): boolean {
    if (race.route.circuit) {
        return true;
    }

    const car = racer.player.getSeat() === 0 ? racer.player.getVehicle() : null;

    return (
        playable(racer.player) &&
        car !== null &&
        car.terminalState === 0 &&
        distance(car.position, race.route.start) <= START_REACH
    );
}

/** The latest the countdown starts: when entries close, or for a street race the gathering's end. */
export function startDeadline(race: Race): number {
    return race.route.circuit ? race.lobbyEndsAt : race.lobbyEndsAt + GATHER_MS;
}

/** Entries have closed and the race waits for its racers at the start line. */
export function gathering(race: Race, now = Date.now()): boolean {
    return race.phase === 'lobby' && !race.route.circuit && now >= race.lobbyEndsAt;
}

function lobbyCard(race: Race, racer: Racer, arrived: number, now: number): void {
    const { route } = race;
    const circuit = Boolean(route.circuit);
    const seated = racer.player.getSeat() === 0;
    const atStart = atStartLine(race, racer);
    const you = circuit
        ? 'You will be put on the grid'
        : atStart
          ? 'At the start line'
          : seated
            ? 'Drive to the red smoke'
            : 'Get a car (/garage)';
    const waiting = gathering(race, now);
    const timing: [string, string] = waiting
        ? ['Starts by', formatMinutes(startDeadline(race) - now)]
        : [circuit ? 'Starts in' : 'Entries close', formatMinutes(race.lobbyEndsAt - now)];

    card(racer.player, 'race', {
        title: route.name,
        subtitle: circuit
            ? `Circuit · ${plural(route.laps ?? 1, 'lap')} · ${lengthText(route)}`
            : `Street race · ${lengthText(route)} · ${route.checkpoints.length} checkpoints`,
        rows: [
            timing,
            circuit
                ? ['Racers', String(race.racers.size)]
                : ['At the start', `${arrived} of ${race.racers.size}`],
            ['You', you],
        ],
        progress: waiting
            ? Math.max(0, (startDeadline(race) - now) / GATHER_MS)
            : Math.max(0, (race.lobbyEndsAt - now) / race.lobbyMs),
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

    if (race.route.circuit) {
        const startsIn = formatMinutes(race.lobbyEndsAt - Date.now());

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
                smoke: true,
                primary: true,
            },
        ]);

        toast(
            player,
            "You're in. Follow the compass to the red smoke at the start line; " +
                'the race waits there until every racer has arrived.',
            { title: race.route.name, tone: 'gold', seconds: 8 },
        );
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
        gatherAnnounced: false,
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

        if (!atStartLine(race, racer)) {
            leaveRace(
                race,
                racer,
                'You did not make it to the start line in a car. The race went without you.',
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

function entriesClosed(race: Race, waitingFor: number): void {
    const message = waitingFor
        ? `Entries are closed. The race waits at the start line for ${plural(waitingFor, 'racer')} ` +
          `still on the way, ${formatMinutes(GATHER_MS)} at most.`
        : 'Entries are closed. Everyone is at the start line.';

    for (const racer of racerList(race)) {
        toast(racer.player, message, { title: race.route.name, tone: 'gold' });
    }
}

export function tickLobby(race: Race, now: number): void {
    for (const racer of racerList(race)) {
        if (takenAway(racer)) {
            leaveRace(race, racer, null);
        }
    }

    const racers = racerList(race);
    const arrived = racers.filter((racer) => atStartLine(race, racer)).length;

    if (now - race.lastStateAt >= 1000) {
        race.lastStateAt = now;

        for (const racer of racers) {
            if (playable(racer.player)) {
                lobbyCard(race, racer, arrived, now);
            }
        }
    }

    if (now < race.lobbyEndsAt) {
        return;
    }

    // Everyone is there, or the wait is over: off they go with those who made it.
    if (arrived === racers.length || now >= startDeadline(race)) {
        startCountdown(race);

        return;
    }

    if (!race.gatherAnnounced) {
        race.gatherAnnounced = true;
        entriesClosed(race, racers.length - arrived);
    }
}
