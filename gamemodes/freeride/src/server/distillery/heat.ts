// Heat: how much the law knows about an operation. Every batch and sale
// raises it; it cools by itself, by a bribe at Salieri's (/rum bribe) or by
// paying Sergeant Doyle when he turns up at the camp. Hot rum sells for less,
// and at full heat the revenue men come for the still (raids.ts).
import { speak } from '../lib/npcs';
import { toast } from '../lib/notify';
import { flatDistance, formatMinutes, formatMoney, onStreet } from '../lib/util';
import { LINE } from './characters';
import { changed, cooldownOf, opKeyOf, opOf, ops, tellOp, TITLE } from './operations';
import { SALIERI_DOOR } from './places';
import { callOffRevenue } from './raids';

export const HEAT_BATCH = 5;
export const HEAT_BLAST = 15;
export const HEAT_REFUSED = 10;
const HEAT_DECAY_MS = 20_000;

export const HEAT_NERVOUS = 75;
export const NERVOUS_PRICE = 0.85;
export const HEAT_DOYLE = 55;
const BRIBE_REACH = 30;
const BRIBE_GAP_MS = 5 * 60_000;
// An envelope at Salieri's costs 3 x heat (40 points off), Doyle 2 x heat
// (50 points off).
const BRIBE_PER_HEAT = 3;
const BRIBE_MIN = 100;

export const DOYLE_PER_HEAT = 2;
export const DOYLE_MIN = 80;

let heatClock = 0;

export function heatWord(heat: number): string {
    if (heat >= 100) {
        return 'raid!';
    }

    if (heat >= HEAT_NERVOUS) {
        return 'hot';
    }

    if (heat >= 40) {
        return 'warm';
    }

    return 'cool';
}

export function addHeat(opKey: string | null, amount: number): void {
    const op = opKey === null ? undefined : ops.get(opKey);

    if (!op || amount <= 0) {
        return;
    }

    const before = op.heat;

    op.heat = Math.min(100, op.heat + amount);
    changed();

    if (before < 50 && op.heat >= 50) {
        tellOp(
            opKey,
            "The revenue men are asking about rum in the lowlands. Lay low or pay someone off (/rum bribe at Salieri's).",
        );
    }

    if (before < HEAT_NERVOUS && op.heat >= HEAT_NERVOUS) {
        tellOp(opKey, 'Your rum is hot: the buyers pay less until things cool down.', 'bad');
    }

    // At 100 the revenue men set out once a member is on the street (raids.ts).
}

/** A point off every operation's heat each HEAT_DECAY_MS of the clock. */
export function cool(dt: number): void {
    heatClock += dt;

    while (heatClock >= HEAT_DECAY_MS) {
        heatClock -= HEAT_DECAY_MS;

        for (const op of ops.values()) {
            // At full heat the raid is coming: only a bribe or the raid itself cools it.
            if (op.heat > 0 && op.heat < 100) {
                op.heat = Math.max(0, op.heat - 1);
                changed();
            }
        }
    }
}

/** /rum bribe: Luigi at Salieri's knows a man at the precinct. */
export function bribe(player: Player): void {
    const op = opOf(player);

    if (!op?.unlocked) {
        toast(player, 'You have nothing to hide yet.', { title: TITLE });

        return;
    }

    if (op.heat < 5) {
        toast(player, 'Nobody is asking about you. Save your money.', { title: TITLE });

        return;
    }

    if (!onStreet(player) || flatDistance(player.position, SALIERI_DOOR) > BRIBE_REACH) {
        toast(
            player,
            "Luigi at Salieri's knows a man at the precinct. Come to Salieri's bar and ask again.",
            { title: TITLE, tone: 'bad' },
        );

        return;
    }

    const opKey = opKeyOf(player);

    if (opKey === null) {
        return;
    }

    const wait = cooldownOf(opKey);

    if (Date.now() - wait.bribeAt < BRIBE_GAP_MS) {
        toast(
            player,
            `Luigi's man was paid ${formatMinutes(Date.now() - wait.bribeAt)} ago. Too soon to ask again.`,
            { title: TITLE, tone: 'bad' },
        );

        return;
    }

    const price = Math.max(BRIBE_MIN, Math.round((op.heat * BRIBE_PER_HEAT) / 10) * 10);

    if (!player.trySpendMoney(price)) {
        toast(player, `The envelope needs ${formatMoney(price)}.`, { title: TITLE, tone: 'bad' });

        return;
    }

    wait.bribeAt = Date.now();
    op.heat = Math.max(0, op.heat - 40);
    const called = callOffRevenue(opKey);

    changed();
    speak(
        player,
        'Luigi',
        called
            ? 'An envelope for the precinct. The revenue men just got new orders: they turn back.'
            : 'An envelope for the precinct. Consider it forgotten, for a while.',
        { voice: LINE.chat, seconds: 6 },
    );
}
