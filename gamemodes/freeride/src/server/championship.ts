// The Autodrom championship, on maps with the official circuit (Lost Heaven
// Extended). A season is three rounds of growing length; anyone can enter any
// round, every finish scores points, and the standings after the last round
// pay out and crown a champion. Champions are remembered between sessions.
import * as store from './lib/store';
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import * as races from './races';
import { toast, toastAll, feed, name, text, COLORS } from './lib/notify';
import { formatMoney, formatDuration, plural } from './lib/util';
import { onMissionChange } from './lib/lifecycle';
import { reward } from './lib/rewards';
import { Delay } from './lib/timers';

const ROUNDS = [
    { laps: 1, name: 'Round 1 · Sprint' },
    { laps: 2, name: 'Round 2 · Feature' },
    { laps: 3, name: 'Round 3 · Grand Prix' },
] as const;
const POINTS = [25, 18, 15, 12, 10, 8, 6, 4, 2, 1];
const PRIZES = [3000, 1500, 750];
const PLACES = ['Champion', 'Runner-up', 'Third'];
const CHAMPION_XP = 400;
const ROUND_LOBBY_MS = 60_000;
const BREAK_MS = 45_000;
const RETRY_MS = 15_000;

interface Score {
    points: number;
    wins: number;
    /** Best finishing time, in ms. */
    best: number | null;
}

interface Season {
    map: maps.MapId;
    /** The next round to run, an index into ROUNDS. */
    round: number;
    /** By nickname, so a driver keeps their points over a reconnect. */
    points: Map<string, Score>;
    raceId: number | null;
    startedBy: string;
}

interface Standing extends Score {
    name: string;
}

interface Champion {
    name: string;
    points: number;
    at: number;
    map: string;
}

interface ChampionshipDocument {
    version: number;
    champions?: Champion[];
}

let season: Season | null = null;
let champions: Champion[] = [];
const nextRound = new Delay();

function load(): void {
    champions = store.load<ChampionshipDocument>('championship')?.champions ?? [];
}

function save(): void {
    store.save('championship', { version: 1, champions } satisfies ChampionshipDocument);
}

// Standings by points, then by wins; names carry over a reconnect.
function standings(): Standing[] {
    return [...(season?.points ?? new Map<string, Score>()).entries()]
        .map(([player, entry]) => ({ name: player, ...entry }))
        .sort((a, b) => b.points - a.points || b.wins - a.wins);
}

function roundName(current: Season): string {
    return ROUNDS[Math.min(current.round, ROUNDS.length - 1)].name;
}

function openRound(): void {
    const map = maps.current();

    if (!season || !races.hasCircuit(map) || map.id !== season.map) {
        if (season) {
            toastAll('The championship was called off: the circuit is not on this map.', {
                title: 'Autodrom',
            });
        }

        season = null;

        return;
    }

    const round = ROUNDS[season.round];
    const route = races.circuitRoute(
        map,
        round.laps,
        `Autodrom Championship · ${round.name}`,
        `${plural(round.laps, 'lap')} of the official circuit.`,
        `b:${map.id}:championship-${round.laps}`,
    );
    const race = races.openRace(null, route, {
        title: 'The Autodrom',
        lobbyMs: ROUND_LOBBY_MS,
        onConclude: scoreRound,
    });

    if (!race) {
        nextRound.start(RETRY_MS, openRound);

        return;
    }

    season.raceId = race.id;
    toastAll(
        `${round.name}: entries close in ${ROUND_LOBBY_MS / 1000} seconds. ` +
            'Type /race join; a race car waits on the grid.',
        { title: 'Autodrom Championship', tone: 'gold', seconds: 10 },
    );
}

function scoreRound(finishers: readonly races.Finisher[]): void {
    const current = season;

    if (!current) {
        return;
    }

    const round = ROUNDS[current.round];

    finishers.forEach(({ player, ms }, index) => {
        const entry = current.points.get(player.nickname) ?? { points: 0, wins: 0, best: null };

        entry.points += POINTS[index] ?? 0;

        if (index === 0) {
            entry.wins++;
        }

        entry.best = entry.best === null ? ms : Math.min(entry.best, ms);
        current.points.set(player.nickname, entry);
    });

    if (finishers.length) {
        feed('race', [
            text(round.name, COLORS.gold),
            text(' won by '),
            name(finishers[0].player, COLORS.white),
            text(` · ${formatDuration(finishers[0].ms)}`),
        ]);
    }

    current.round++;
    const table = standings();
    const lines = table
        .slice(0, 3)
        .map((entry, index) => `${index + 1}. ${entry.name} ${entry.points}`)
        .join(' · ');

    if (current.round < ROUNDS.length) {
        toastAll(
            `Standings: ${lines || 'nobody has scored yet'}. ` +
                `${ROUNDS[current.round].name} opens in ${BREAK_MS / 1000} seconds.`,
            { title: 'Autodrom Championship' },
        );

        nextRound.start(BREAK_MS, openRound);

        return;
    }

    finishSeason(current, table);
}

function finishSeason(ended: Season, table: readonly Standing[]): void {
    season = null;

    if (!table.length) {
        toastAll('The championship ends with nobody across the line.', {
            title: 'Autodrom Championship',
        });

        return;
    }

    table.slice(0, PRIZES.length).forEach((entry, index) => {
        const player = World.getPlayers().find((other) => other.nickname === entry.name);

        if (!player) {
            return;
        }

        reward(
            player,
            index === 0
                ? {
                      money: PRIZES[index],
                      counters: ['championships'],
                      xp: CHAMPION_XP,
                      reason: 'Autodrom champion',
                  }
                : { money: PRIZES[index] },
        );

        toast(player, `${PLACES[index]} of the Autodrom: ${formatMoney(PRIZES[index])}.`, {
            title: 'Autodrom Championship',
            tone: 'gold',
        });
    });

    champions.unshift({
        name: table[0].name,
        points: table[0].points,
        at: Date.now(),
        map: ended.map,
    });

    champions = champions.slice(0, 20);
    save();
    toastAll(`${table[0].name} is the Autodrom champion with ${table[0].points} points.`, {
        title: 'Autodrom Championship',
        tone: 'gold',
        seconds: 10,
    });

    feed('record', [text(table[0].name, COLORS.gold), text(' won the Autodrom Championship')]);
}

commands.register(
    {
        name: 'championship',
        aliases: ['champ'],
        usage: '/championship [start|status]',
        help: 'Run a three-round championship on the official circuit.',
        category: 'Races',
    },
    (player, args) => {
        const action = (args[0] ?? 'status').toLowerCase();

        if (action === 'start') {
            if (season) {
                toast(
                    player,
                    'A championship is already under way. /championship shows the standings.',
                    { tone: 'bad' },
                );

                return;
            }

            const map = maps.current();

            if (!map?.circuit) {
                toast(
                    player,
                    'There is no official circuit on this map. Vote for Lost Heaven Extended with /map.',
                    { tone: 'bad' },
                );

                return;
            }

            season = {
                map: map.id,
                round: 0,
                points: new Map(),
                raceId: null,
                startedBy: player.nickname,
            };

            feed('race', [name(player, COLORS.gold), text(' started an Autodrom Championship')]);
            openRound();

            return;
        }

        const table = standings()
            .map((entry, index) => `${index + 1}. ${entry.name} ${entry.points}`)
            .join(' · ');

        player.sendMessage(
            season
                ? `Championship, ${roundName(season)}: ${table || 'no points yet'}`
                : `No championship running. ${champions[0] ? `Last champion: ${champions[0].name}.` : ''} ` +
                      '/championship start begins one.',
            0xd9cdb4,
        );
    },
);

menu.provide('championship', () => ({
    available: Boolean(maps.current()?.circuit),
    running: season
        ? {
              round: Math.min(season.round + 1, ROUNDS.length),
              rounds: ROUNDS.length,
              name: roundName(season),
              standings: standings().slice(0, 8),
          }
        : null,
    champions: champions.slice(0, 5),
}));

load();

onMissionChange(() => {
    if (!season) {
        return;
    }

    season = null;
    nextRound.cancel();
});
