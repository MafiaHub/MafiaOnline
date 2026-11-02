// Admin chat commands (admins are granted from the console, lib/admin.ts):
// /givemoney hands a player cash, or takes it with a negative amount. The
// console has its own "fr money" (console.ts).
import * as admin from '../lib/admin';
import * as commands from '../lib/commands';
import * as log from '../lib/log';
import { toast } from '../lib/notify';
import { findPlayer, formatMoney } from '../lib/util';

// "$1,500", "-200" and "+50" all read as whole dollars.
function dollars(text: string | undefined): number | null {
    const cleaned = (text ?? '').replace(/[$,_]/g, '');

    return /^[+-]?\d+$/.test(cleaned) ? Number(cleaned) : null;
}

commands.register(
    {
        name: 'givemoney',
        aliases: ['money'],
        usage: '/givemoney [player] <amount>',
        help: 'Admins: give a player money; a negative amount takes it.',
        category: 'Dev',
        hidden: true,
    },
    (player, args) => {
        if (!admin.isAdmin(player)) {
            toast(player, 'Only server admins can hand out money.', { tone: 'bad' });

            return;
        }

        const amount = dollars(args.at(-1));
        const who = args.slice(0, -1).join(' ');
        const target = who ? findPlayer(who) : player;

        if (amount === null || amount === 0) {
            player.sendMessage(
                'Use /givemoney [player] <amount>, e.g. /givemoney Tommy 5000',
                0xd9cdb4,
            );

            return;
        }

        if (!target) {
            toast(player, `No player named "${who}" is in town.`, { tone: 'bad' });

            return;
        }

        if (!target.giveMoney(amount)) {
            toast(
                player,
                amount < 0
                    ? `${target.nickname} has only ${formatMoney(target.money)}.`
                    : 'That would take them past the maximum balance.',
                { tone: 'bad' },
            );

            return;
        }

        const change = `${amount < 0 ? '-' : '+'}${formatMoney(Math.abs(amount))}`;

        log.info(
            `${player.nickname} gave ${target.nickname} ${change}; balance ${formatMoney(target.money)}`,
        );

        toast(player, `${target.nickname}: ${change}, now ${formatMoney(target.money)}.`, {
            tone: 'good',
        });

        if (target.id !== player.id) {
            toast(
                target,
                amount > 0
                    ? `An admin gave you ${formatMoney(amount)}.`
                    : `An admin took ${formatMoney(-amount)} from you.`,
                { tone: amount > 0 ? 'good' : 'bad' },
            );
        }
    },
);
