// Street races. Anyone opens a lobby on a route; everyone in the city is
// invited, drives to the start line and is put on a starting grid. The server
// places the cars, runs the countdown, checks every checkpoint against the
// car's replicated position and pays the podium. Built-in routes follow the
// stock road graph; players can record their own with /race create.
//
// routes.ts is the route book, lobby.ts the lobby and the grid, run.ts the
// race itself, results.ts the payout and recorder.ts route recording.
import * as menu from '../lib/menu';
import * as commands from '../lib/commands';
import { toast } from '../lib/notify';
import { playable, emit, pick, formatDuration } from '../lib/util';
import { events, every, onMissionChange, onStart, onStop } from '../lib/lifecycle';
import { allRoutes, findRoute, lengthText, load, recordOf, type Route } from './routes';
import { races, raceOf, openLobby, racerList, leaveRace, closeRace } from './race';
import { join, openRace, tickLobby } from './lobby';
import { tickCountdown, tickRunning } from './run';
import {
    checkpoint,
    create,
    deleteRoute,
    discard,
    recordings,
    saveRecording,
    tickRecordings,
    undo,
} from './recorder';

export { circuitRoute, hasCircuit } from './routes';
export { raceOf, type Finisher } from './race';
export { openRace, type RaceOptions } from './lobby';

const TICK_MS = 200;

type Subcommand = (player: Player, args: string[]) => void;

function routeSummary(route: Route, index: number): string {
    const record = recordOf(route.id);

    return (
        `${index + 1}. ${route.name} · ${lengthText(route)}` +
        (record ? ` · record ${formatDuration(record.ms)} by ${record.name}` : '')
    );
}

function start(player: Player, args: string[]): void {
    const route = args.length
        ? findRoute(args.join(' '))
        : pick(allRoutes().filter((entry) => entry.builtIn));

    if (!route) {
        toast(player, 'No such route. /race list shows them.', { tone: 'bad' });

        return;
    }

    openRace(player, route);
}

function joinRace(player: Player, args: string[]): void {
    const race = args.length ? races.get(Number(args[0])) : openLobby();

    if (!race || race.phase !== 'lobby') {
        toast(player, 'No race is taking entries. Open one with /race start.', { tone: 'bad' });

        return;
    }

    if (playable(player)) {
        join(race, player);
    }
}

const subcommands: Record<string, Subcommand> = {
    start,
    join: joinRace,
    leave(player) {
        const race = raceOf(player);
        const racer = race?.racers.get(player.id);

        if (race && racer) {
            leaveRace(race, racer, 'You left the race.');
        }
    },
    list(player) {
        allRoutes()
            .slice(0, 12)
            .forEach((route, index) => player.sendMessage(routeSummary(route, index), 0xd9cdb4));

        player.sendMessage('Open a lobby with /race start <number or name>.', 0xe8c57d);
    },
    create,
    cp: checkpoint,
    undo,
    save: saveRecording,
    discard,
    delete: deleteRoute,
};

commands.register(
    {
        name: 'race',
        aliases: ['races'],
        usage: '/race [start|join|leave|list|create|cp|undo|save|discard|delete]',
        help: 'Open, join or record street races.',
        category: 'Races',
    },
    (player, args) => {
        const action = (args[0] ?? '').toLowerCase();

        if (Object.hasOwn(subcommands, action)) {
            subcommands[action](player, args.slice(1));

            return;
        }

        if (!action) {
            if (openLobby() && !raceOf(player)) {
                joinRace(player, []);
            } else {
                emit(player, 'fr:menu:open', { tab: 'races' });
            }

            return;
        }

        start(player, args);
    },
);

menu.provide('races', (player) => {
    const recording = recordings.get(player.id);

    return {
        routes: allRoutes().map((route, index) => ({
            index: index + 1,
            id: route.id,
            name: route.name,
            description: route.description,
            length: Math.round(route.length),
            checkpoints: route.checkpoints.length,
            builtIn: route.builtIn,
            mine: route.author === player.nickname,
            record: recordOf(route.id),
        })),
        races: [...races.values()].map((race) => ({
            id: race.id,
            route: race.route.name,
            host: race.host,
            phase: race.phase,
            racers: racerList(race).map((racer) => racer.player.nickname),
            secondsLeft: Math.max(0, Math.ceil((race.lobbyEndsAt - Date.now()) / 1000)),
            joined: race.racers.has(player.id),
        })),
        recording: recording
            ? { name: recording.name, checkpoints: recording.checkpoints.length }
            : null,
    };
});

function tick(): void {
    const now = Date.now();

    for (const race of [...races.values()]) {
        if (race.generation !== World.getMissionGeneration()) {
            closeRace(race);
            continue;
        }

        for (const racer of racerList(race)) {
            if (!World.getPlayers().some((player) => player.id === racer.player.id)) {
                race.racers.delete(racer.player.id);
            }
        }

        if (race.phase === 'lobby') {
            tickLobby(race, now);
        } else if (race.phase === 'countdown') {
            tickCountdown(race, now);
        } else {
            tickRunning(race, now);
        }

        // An empty lobby waits for its timer: a scripted one starts with nobody in it.
        if (races.has(race.id) && !race.racers.size && race.phase !== 'lobby') {
            races.delete(race.id);
        }
    }

    tickRecordings(now);
}

function closeAll(): void {
    for (const race of [...races.values()]) {
        closeRace(race);
    }
}

onStart(load);
every(TICK_MS, tick);
onStop(closeAll);
onMissionChange(closeAll);

events.on('playerDeath', (player) => {
    const race = raceOf(player);
    const racer = race?.racers.get(player.id);

    if (race && racer) {
        leaveRace(race, racer, 'You were taken out of the race.');
    }
});

events.on('playerDisconnect', (player) => {
    raceOf(player)?.racers.delete(player.id);
});
