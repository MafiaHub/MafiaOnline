// A finished batch goes into a cask to age; an aged cask is bottled (G held
// at the casks) into cases that wait in the shed for a car.
import * as profiles from '../lib/profiles';
import { toast } from '../lib/notify';
import { plural } from '../lib/util';
import { hankSays, LINE } from './characters';
import { changed, MAX_CASES, ops, QUEST, tellOp, TITLE, XP, type Operation } from './operations';
import { CASKS } from './places';

export const CASK_AGE_MS = 150_000;
const CASES_PER_CASK = 4;
const POUR = '014a_nalevani.wav';

export function age(dt: number): void {
    for (const [opKey, op] of ops) {
        if (!op.casks.length) {
            continue;
        }

        let ready = 0;

        for (const cask of op.casks) {
            if (cask.left <= 0) {
                continue;
            }

            cask.left -= dt;

            if (cask.left <= 0) {
                ready++;
            }
        }

        changed();

        if (ready) {
            tellOp(
                opKey,
                `${ready === 1 ? 'A cask has' : `${ready} casks have`} aged. Hold G at the casks in the clearing to bottle it.`,
                'good',
            );
        }
    }
}

/** The index of an aged cask, or -1. */
export function agedCask(op: Operation): number {
    return op.casks.findIndex((cask) => cask.left <= 0);
}

/** How long until the next cask is aged, or null when none is ageing. */
export function nextAged(op: Operation): number | null {
    const ageing = op.casks.filter((cask) => cask.left > 0);

    return ageing.length ? Math.min(...ageing.map((cask) => cask.left)) : null;
}

export function bottle(player: Player, op: Operation): void {
    const index = agedCask(op);
    const room = Math.max(0, MAX_CASES - op.cases.n);

    if (index < 0 || !room) {
        return;
    }

    const [cask] = op.casks.splice(index, 1);
    const count = Math.min(CASES_PER_CASK, room);

    op.cases.n += count;
    op.cases.q += count * cask.q;
    changed();

    if (typeof Sound !== 'undefined') {
        Sound.play(POUR, CASKS, { radius: 20, volume: 0.8 });
    }

    profiles.addXp(player, XP.bottle, 'Bottled rum');

    if (op.progress === QUEST.FIRST_FIRE) {
        op.progress = QUEST.FIRST_SALE;
        hankSays(
            player,
            "Your first rum! Load it into a car out in the clearing: stop it by the shed and sit still. Luigi at Salieri's buys, " +
                "and so does the speakeasy at Morello's, for more and a risk.",
            LINE.nod,
            10,
        );

        profiles.addXp(player, XP.batch, 'First rum bottled');
    } else {
        toast(
            player,
            `${plural(count, 'case')} bottled (quality ${cask.q}%). ${plural(op.cases.n, 'case')} wait in the shed.`,
            { title: TITLE, tone: 'good' },
        );
    }
}
