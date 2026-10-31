// The everyday commands: /help and /stats, looking after a car (/repair,
// /flip, /engine) and /kill.
import * as commands from '../lib/commands';
import * as profiles from '../lib/profiles';
import * as life from '../lib/life';
import * as maps from '../lib/maps';
import { toast } from '../lib/notify';
import { distance, playable, findPlayer, formatMoney, headingOf, speedOf } from '../lib/util';
import { Cooldown } from '../lib/cooldowns';
import { currentOrLastVehicle } from './cars';

const REPAIR_COOLDOWN_MS = 45_000;
// A mechanic comes out anywhere; the pump and the car lot charge less.
const REPAIR_COST = 150;
const REPAIR_COST_AT_GARAGE = 50;
const GARAGE_REACH = 25;
const FLIP_COOLDOWN_MS = 10_000;
const CAR_REACH = 8;

const repairs = new Cooldown(REPAIR_COOLDOWN_MS);
const flips = new Cooldown(FLIP_COOLDOWN_MS);

function nearbyCar(player: Player): Vehicle | null {
    const vehicle = currentOrLastVehicle(player);

    if (!vehicle || !vehicle.model || vehicle.terminalState !== 0) {
        toast(player, 'Get in a car, or call one of yours with /garage.', { tone: 'bad' });

        return null;
    }

    if (!player.getVehicle() && distance(player.position, vehicle.position) > CAR_REACH) {
        toast(player, 'Walk up to your car first.', { tone: 'bad' });

        return null;
    }

    return vehicle;
}

function statsLine(record: profiles.PlayerRecord): string {
    const { level, rank } = profiles.progress(record);
    const ratio = record.deaths
        ? (record.kills / record.deaths).toFixed(2)
        : record.kills.toFixed(2);
    const hours = Math.round((record.playMs / 3_600_000) * 10) / 10;

    return (
        `${record.name}: level ${level} ${rank}, ${record.kills} kills / ${record.deaths} deaths (${ratio}), ` +
        `${record.racesWon} race wins, ${record.deliveries} deliveries, ${hours} h played`
    );
}

commands.register(
    {
        name: 'help',
        aliases: ['commands'],
        usage: '/help',
        help: 'List the commands in chat.',
        category: 'City',
    },
    (player) => {
        const byCategory = new Map<string, string[]>();

        for (const entry of commands.list()) {
            if (entry.category === 'Dev') {
                continue;
            }

            const names = byCategory.get(entry.category) ?? [];

            names.push(`/${entry.name}`);
            byCategory.set(entry.category, names);
        }

        player.sendMessage('Free Ride · F2 city menu · F1 players · F9 radio', 0xe8c57d);

        for (const [category, names] of byCategory) {
            player.sendMessage(`${category}: ${names.join(' ')}`, 0xd9cdb4);
        }
    },
);

commands.register(
    { name: 'stats', usage: '/stats [player]', help: 'Show a career record.', category: 'City' },
    (player, args) => {
        const target = args.length ? findPlayer(args.join(' ')) : player;
        const record = target && profiles.get(target);

        if (!record) {
            toast(player, 'No such player in town.', { tone: 'bad' });

            return;
        }

        player.sendMessage(statsLine(record), 0xe8c57d);
    },
);

commands.register(
    {
        name: 'repair',
        aliases: ['fix'],
        usage: '/repair',
        help: 'Pay a mechanic to fix your car; cheaper at the pump or the car lot.',
        category: 'Cars',
    },
    (player) => {
        if (!playable(player)) {
            return;
        }

        const vehicle = nearbyCar(player);

        if (!vehicle) {
            return;
        }

        const garages = maps
            .places()
            .filter((place) => place.key === 'pump' || place.key === 'bertone');
        const cost = garages.some(
            (place) => distance(vehicle.position, place.position) <= GARAGE_REACH,
        )
            ? REPAIR_COST_AT_GARAGE
            : REPAIR_COST;

        if (repairs.take(player) > 0) {
            toast(player, 'The mechanic is still busy with the last job.', { tone: 'bad' });

            return;
        }

        if (!player.trySpendMoney(cost)) {
            toast(player, `The mechanic wants ${formatMoney(cost)}.`, { tone: 'bad' });

            return;
        }

        if (vehicle.repair()) {
            toast(player, `Good as new, for ${formatMoney(cost)}.`, { tone: 'good' });
        } else {
            player.giveMoney(cost);
            toast(player, 'This car cannot be repaired.', { tone: 'bad' });
        }
    },
);

commands.register(
    {
        name: 'flip',
        usage: '/flip',
        help: 'Put an overturned car back on its wheels.',
        category: 'Cars',
    },
    (player) => {
        if (!playable(player)) {
            return;
        }

        const vehicle = nearbyCar(player);

        if (!vehicle) {
            return;
        }

        if (speedOf(vehicle) > 2) {
            toast(player, 'Let the car come to a stop first.', { tone: 'bad' });

            return;
        }

        if (flips.take(player) > 0) {
            return;
        }

        const { x, y, z } = vehicle.position;

        vehicle.setTransform({ x, y: y + 1.2, z }, headingOf(vehicle.rotation), {
            x: 0,
            y: 0,
            z: 0,
        });

        toast(player, 'Back on four wheels.', { tone: 'good' });
    },
);

commands.register(
    {
        name: 'engine',
        usage: '/engine',
        help: "Start or stop the engine from the driver's seat.",
        category: 'Cars',
    },
    (player) => {
        const vehicle = player.getVehicle();

        if (!vehicle || player.getSeat() !== 0) {
            toast(player, "Sit in the driver's seat first.", { tone: 'bad' });

            return;
        }

        vehicle.setEngine(!vehicle.engineOn);
    },
);

commands.register(
    {
        name: 'kill',
        aliases: ['suicide'],
        usage: '/kill',
        help: 'End this life and respawn.',
        category: 'City',
    },
    (player) => {
        if (!player.alive) {
            return;
        }

        const busy = life.activity(player);

        if (busy?.kind === 'duel' || busy?.kind === 'jail') {
            toast(
                player,
                busy.kind === 'jail'
                    ? 'Serve your time or pay /bail.'
                    : 'Not in the middle of a duel.',
                { tone: 'bad' },
            );

            return;
        }

        player.setHealth(0);
    },
);
