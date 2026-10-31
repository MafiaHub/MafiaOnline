// Money between players and a small wage for being out on the street. Jobs
// are where the real money is; the wage keeps a broke player moving.
import * as commands from './lib/commands';
import { toast, feed, name, text, COLORS } from './lib/notify';
import { distance, playable, findPlayer, formatMoney } from './lib/util';
import { every } from './lib/lifecycle';
import { PlayerMap } from './lib/players';

const PAY_REACH = 6;
const MAX_PAYMENT = 50_000;
const WAGE = 30;
const WAGE_EVERY_MS = 10 * 60_000;

// When each spawned player's current wage period began.
const sinceWage = new PlayerMap<number>();

commands.register(
    {
        name: 'pay',
        aliases: ['give'],
        usage: '/pay <player> <amount>',
        help: 'Hand cash to someone standing next to you.',
        category: 'Social',
    },
    (player, args) => {
        const target = findPlayer(args[0]);
        const amount = Math.floor(Number(args[1]));

        if (
            !target ||
            target.id === player.id ||
            !Number.isFinite(amount) ||
            amount < 1 ||
            amount > MAX_PAYMENT
        ) {
            toast(player, 'Use /pay <player> <amount>.', { tone: 'bad' });

            return;
        }

        if (
            !playable(player) ||
            !playable(target) ||
            player.virtualWorld !== target.virtualWorld ||
            distance(player.position, target.position) > PAY_REACH
        ) {
            toast(player, 'Hand cash over in person: stand next to them.', { tone: 'bad' });

            return;
        }

        if (!player.trySpendMoney(amount)) {
            toast(player, `You only have ${formatMoney(player.money)}.`, { tone: 'bad' });

            return;
        }

        if (!target.giveMoney(amount)) {
            player.giveMoney(amount);
            toast(player, `${target.nickname} cannot carry that much.`, { tone: 'bad' });

            return;
        }

        toast(player, `You handed ${target.nickname} ${formatMoney(amount)}.`, { tone: 'good' });
        toast(target, `${player.nickname} handed you ${formatMoney(amount)}.`, { tone: 'good' });

        if (amount >= 1000) {
            feed('pay', [
                name(player, COLORS.gold),
                text(' paid '),
                name(target),
                text(` ${formatMoney(amount)}`, COLORS.gold),
            ]);
        }
    },
);

function payWages(): void {
    const now = Date.now();

    for (const player of World.getPlayers()) {
        if (!player.spawned) {
            sinceWage.delete(player.id);
            continue;
        }

        const since = sinceWage.get(player.id) ?? now;

        sinceWage.set(player.id, since);

        if (now - since < WAGE_EVERY_MS) {
            continue;
        }

        sinceWage.set(player.id, now);
        player.giveMoney(WAGE);
        toast(player, `Odd jobs around town earned you ${formatMoney(WAGE)}.`, { seconds: 3 });
    }
}

every(30_000, payWages);
