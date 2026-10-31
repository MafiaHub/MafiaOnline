// Dice between two players at the same table: both put the bet down and the
// server rolls two dice for each.
import * as commands from '../lib/commands';
import { toast, feed, name, text, COLORS } from '../lib/notify';
import { distance, playable, findPlayer, formatMoney } from '../lib/util';
import { challenge, collectStakes } from '../lib/challenges';

const DICE_REACH = 15;
const MIN_BET = 10;
const MAX_BET = 1000;

function roll(): number {
    return 2 + Math.floor(Math.random() * 6) + Math.floor(Math.random() * 6);
}

function playDice(challenger: Player, opponent: Player, bet: number): void {
    const near =
        playable(challenger) &&
        playable(opponent) &&
        distance(challenger.position, opponent.position) <= DICE_REACH;

    if (!near) {
        toast(challenger, 'You need to be at the same table.', { tone: 'bad' });
        toast(opponent, 'You need to be at the same table.', { tone: 'bad' });

        return;
    }

    const short = collectStakes([challenger, opponent], bet);

    if (short === challenger) {
        toast(opponent, `${challenger.nickname} cannot cover the bet.`, { tone: 'bad' });

        return;
    }

    if (short) {
        toast(challenger, `${opponent.nickname} cannot cover the bet.`, { tone: 'bad' });

        return;
    }

    const a = roll();
    const b = roll();
    const line = `${challenger.nickname} rolls ${a}, ${opponent.nickname} rolls ${b}.`;

    if (a === b) {
        challenger.giveMoney(bet);
        opponent.giveMoney(bet);

        for (const player of [challenger, opponent]) {
            toast(player, `${line} A push; bets back.`, { title: 'Dice' });
        }

        return;
    }

    const [winner, loser] = a > b ? [challenger, opponent] : [opponent, challenger];

    winner.giveMoney(bet * 2);
    toast(winner, `${line} You take ${formatMoney(bet * 2)}.`, { title: 'Dice', tone: 'good' });
    toast(loser, `${line} ${winner.nickname} takes the pot.`, { title: 'Dice', tone: 'bad' });
    feed('dice', [
        name(winner, COLORS.gold),
        text(` won ${formatMoney(bet * 2)} at dice from `),
        name(loser),
    ]);
}

commands.register(
    {
        name: 'dice',
        usage: '/dice <player> <bet>',
        help: 'Bet someone nearby on a roll of the dice.',
        category: 'Social',
    },
    (player, args) => {
        const target = findPlayer(args[0]);
        const bet = Math.floor(Number(args[1]));

        if (
            !target ||
            target.id === player.id ||
            !Number.isFinite(bet) ||
            bet < MIN_BET ||
            bet > MAX_BET
        ) {
            toast(player, `Use /dice <player> <bet from ${MIN_BET} to ${MAX_BET}>.`, {
                tone: 'bad',
            });

            return;
        }

        if (player.money < bet) {
            toast(player, 'You cannot cover that bet.', { tone: 'bad' });

            return;
        }

        challenge(
            target,
            {
                kind: 'dice',
                from: player,
                summary: `Dice for ${formatMoney(bet)}`,
                text: `${player.nickname} bets you ${formatMoney(bet)} on a roll of the dice.`,
                accept: (opponent, challenger) => playDice(challenger, opponent, bet),
            },
            `Bet offered to ${target.nickname}.`,
        );
    },
);
