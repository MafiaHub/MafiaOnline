// Asking for a ride to a friend: /goto puts the request to them, and on
// /accept the traveller appears behind them, or in a free seat of their car.
import * as life from '../lib/life';
import * as commands from '../lib/commands';
import { toast } from '../lib/notify';
import { MAIN_WORLD, playable, ahead, headingOf } from '../lib/util';
import { events } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';
import { Cooldown } from '../lib/cooldowns';
import { challenge, opponent } from '../lib/challenges';

const GOTO_COOLDOWN_MS = 60_000;
const RECENT_DAMAGE_MS = 15_000;

const lastDamage = new PlayerMap<number>();
const rides = new Cooldown(GOTO_COOLDOWN_MS);

function blocked(player: Player): string | null {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) {
        return 'You need to be out on the street.';
    }

    if (life.activity(player)) {
        return life.busyMessage(player);
    }

    if ((Police.getRecord(player)?.level ?? 0) > 0) {
        return 'Not with the police after you.';
    }

    if (Date.now() - (lastDamage.get(player.id) ?? 0) < RECENT_DAMAGE_MS) {
        return 'Not in the middle of a fight.';
    }

    return null;
}

function bring(traveller: Player, host: Player): void {
    const problem =
        blocked(traveller) ??
        (playable(host) && host.virtualWorld === MAIN_WORLD
            ? null
            : `${host.nickname} is not on the street.`);

    if (problem) {
        toast(traveller, problem, { tone: 'bad' });
        toast(host, `${traveller.nickname} could not come: ${problem}`, { tone: 'bad' });

        return;
    }

    const car = host.getVehicle();
    const seat = car ? car.getOccupants().findIndex((occupant) => occupant === null) : -1;
    const heading = headingOf(host.rotation);
    const behind = car ? 4 : 1.6;

    if (traveller.getVehicle()) {
        traveller.removeFromVehicle();
    }

    if (!life.teleport(traveller, ahead((car ?? host).position, heading, -behind, 0.2), heading)) {
        toast(traveller, 'The ride fell through.', { tone: 'bad' });

        return;
    }

    rides.start(traveller);

    if (car && seat > 0) {
        traveller.putInVehicle(car, seat);
    }

    toast(traveller, `You caught up with ${host.nickname}.`, { tone: 'good' });
    toast(host, `${traveller.nickname} is here.`, { tone: 'good' });
}

commands.register(
    {
        name: 'goto',
        aliases: ['tp'],
        usage: '/goto <player>',
        help: 'Ask a player to bring you to them.',
        category: 'Social',
    },
    (player, args) => {
        const target = opponent(player, args.join(' '));

        if (!target) {
            return;
        }

        const problem = blocked(player);

        if (problem) {
            toast(player, problem, { tone: 'bad' });

            return;
        }

        const wait = rides.remaining(player);

        if (wait > 0) {
            toast(player, `Your driver needs ${Math.ceil(wait / 1000)} more seconds.`, {
                tone: 'bad',
            });

            return;
        }

        challenge(
            target,
            {
                kind: 'goto',
                from: player,
                summary: 'Wants to come to you',
                text: `${player.nickname} wants to come to you.`,
                accept: (host, traveller) => bring(traveller, host),
            },
            `Asked ${target.nickname} for a ride.`,
        );
    },
);

events.on('playerDamage', (victim, { attacker }) => {
    lastDamage.set(victim.id, Date.now());

    if (attacker) {
        lastDamage.set(attacker.id, Date.now());
    }
});
