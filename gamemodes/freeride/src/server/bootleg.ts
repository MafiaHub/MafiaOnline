// Bootleg runs, on maps with both the countryside and the city: drive out to
// the still in the hills, wait while the crates are loaded, then bring them
// to Salieri's bar before the whisky is missed. Loading the car tips off the
// police. Any car will do, but it has to arrive in one piece.
import * as maps from './lib/maps';
import * as roads from './lib/roads';
import * as life from './lib/life';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { toast, card, name, text, COLORS, type Tone } from './lib/notify';
import {
    flatDistance,
    formatMoney,
    formatMinutes,
    playable,
    playerById,
    vehicleById,
    type Point,
} from './lib/util';
import { every, onMissionChange } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { Countdown, beacon, clearHud, lead, stoppedAt } from './lib/errands';
import { reward } from './lib/rewards';

const STILL = 'hills';
const BUYER = 'salieri';
const HUD = 'bootleg';
const COMPASS_PRIORITY = 65;
const STOP_RADIUS = 16;
const STOP_SPEED = 2;
const LOAD_MS = 8000;
const DELIVERY_MS = 10 * 60_000;
const PAY = 700;
const XP = 120;
const TICK_MS = 500;

type Run =
    | { phase: 'drive' }
    | { phase: 'load'; carId: number; load: Countdown }
    | { phase: 'deliver'; carId: number; deadline: Countdown };

const runs = new PlayerMap<Run>();

function available(): boolean {
    const places = maps.current()?.places ?? [];

    return places.includes(STILL) && places.includes(BUYER);
}

function end(player: Player, message: string | null, tone: Tone = 'bad'): void {
    if (!runs.has(player.id)) {
        return;
    }

    runs.delete(player.id);
    life.end(player, 'bootleg');
    clearHud(player, HUD, COMPASS_PRIORITY);

    if (message) {
        toast(player, message, { title: 'Bootleg run', tone });
    }
}

// Cars stop on the road nearest each landmark, not at the building itself.
function stop(key: typeof STILL | typeof BUYER): Point {
    const graph = roads.current();

    return graph.crossings[graph.nearestCrossing(maps.landmark(key).position)];
}

function subtitleOf(run: Run): string {
    switch (run.phase) {
        case 'deliver':
            return "Bring the whisky to Salieri's bar";
        case 'load':
            return 'Loading at the still';
        default:
            return 'Drive to the still in the hills';
    }
}

// One step of the run towards `goal`; false when it ended.
function advance(player: Player, run: Run, goal: Point, now: number): boolean {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;

    if (run.phase === 'deliver') {
        const loaded = vehicleById(run.carId);

        if (!loaded || loaded.terminalState !== 0) {
            end(player, 'The car is wrecked and the whisky with it.');

            return false;
        }

        if (run.deadline.over(now)) {
            end(player, 'Too slow. Salieri found another supplier for tonight.');

            return false;
        }
    }

    const stopped = car !== null && stoppedAt(car, goal, STOP_RADIUS, STOP_SPEED);

    if (run.phase === 'drive') {
        if (stopped) {
            runs.set(player.id, {
                phase: 'load',
                carId: car.id,
                load: new Countdown(LOAD_MS, now),
            });

            toast(player, 'The boys are loading the crates. Keep the engine running.', {
                title: 'Bootleg run',
            });
        }
    } else if (run.phase === 'load') {
        if (!stopped || car.id !== run.carId) {
            runs.set(player.id, { phase: 'drive' });
            toast(player, 'You pulled away before the crates were in.', {
                title: 'Bootleg run',
                tone: 'bad',
            });
        } else if (run.load.over(now)) {
            runs.set(player.id, {
                phase: 'deliver',
                carId: run.carId,
                deadline: new Countdown(DELIVERY_MS, now),
            });

            lead(
                player,
                HUD,
                COMPASS_PRIORITY,
                beacon(BUYER, stop(BUYER), "DELIVER TO SALIERI'S", COLORS.green),
            );

            Police.setWanted(player, 1);
            toast(
                player,
                `Loaded. Get it to Salieri's in ${formatMinutes(DELIVERY_MS)}. Somebody saw you.`,
                { title: 'Bootleg run', tone: 'gold' },
            );
        }
    } else if (stopped && car.id === run.carId) {
        reward(player, {
            money: PAY,
            counters: ['deliveries'],
            xp: XP,
            reason: 'Bootleg run',
            feed: {
                kind: 'bootleg',
                parts: [
                    name(player, COLORS.gold),
                    text(" brought a load of whisky to Salieri's · "),
                    text(formatMoney(PAY), COLORS.gold),
                ],
            },
        });

        end(player, `Salieri pays ${formatMoney(PAY)} for the load.`, 'good');

        return false;
    }

    return true;
}

function tick(): void {
    const now = Date.now();

    for (const [id, previous] of runs) {
        const player = playerById(id);

        if (!player) {
            runs.delete(id);
            continue;
        }

        if (!playable(player) || !available()) {
            end(player, 'The run is off.');
            continue;
        }

        // The card goes by the goal the step began with.
        const goal = stop(previous.phase === 'deliver' ? BUYER : STILL);

        if (!advance(player, previous, goal, now)) {
            continue;
        }

        const run = runs.get(id) ?? previous;
        const rows: [string, string][] = [
            ['Distance', `${Math.round(flatDistance(player.position, goal))} m`],
        ];

        if (run.phase === 'deliver') {
            rows.push(['Time left', formatMinutes(run.deadline.left(now))]);
        }

        rows.push(['Pay', formatMoney(PAY)]);
        card(player, HUD, {
            title: 'Bootleg run',
            subtitle: subtitleOf(run),
            rows,
            progress:
                run.phase === 'load'
                    ? run.load.progress(now)
                    : run.phase === 'deliver'
                      ? 1 - run.deadline.progress(now)
                      : undefined,
            tone: 'gold',
        });
    }
}

commands.register(
    {
        name: 'bootleg',
        aliases: ['whisky'],
        usage: '/bootleg [cancel]',
        help: "Run whisky from the hills to Salieri's (Lost Heaven Extended).",
        category: 'City',
    },
    (player, args) => {
        if (args[0]?.toLowerCase() === 'cancel') {
            end(player, 'You called the run off.');

            return;
        }

        if (runs.has(player.id) || !playable(player)) {
            return;
        }

        if (!available()) {
            toast(player, 'There are no stills on this map. Play Lost Heaven Extended (/map).', {
                tone: 'bad',
            });

            return;
        }

        if (!life.begin(player, 'bootleg', 'Bootleg run')) {
            toast(player, life.busyMessage(player) ?? 'You are busy.', { tone: 'bad' });

            return;
        }

        runs.set(player.id, { phase: 'drive' });
        lead(player, HUD, COMPASS_PRIORITY, beacon(STILL, stop(STILL), 'THE STILL', COLORS.gold));
        toast(
            player,
            'Salieri needs whisky. Drive out to the still in the hills and stop at the marker.',
            { title: 'Bootleg run', tone: 'gold' },
        );
    },
);

menu.provide('bootleg', (player) => ({
    available: available(),
    active: runs.has(player.id),
    pay: PAY,
}));

every(TICK_MS, tick);
onMissionChange(() => {
    for (const player of World.getPlayers()) {
        end(player, null);
    }
});
