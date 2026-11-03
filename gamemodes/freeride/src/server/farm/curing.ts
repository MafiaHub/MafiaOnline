// The barn: leaves carried (or driven) to its arrow are hung up to cure, and
// cure while their grower works on, online or not.
import { toast } from '../lib/notify';
import { flatDistance, formatMinutes, plural } from '../lib/util';
import { TITLE, ownerOnline } from './owners';
import { siteNow } from './sites';
import { changed, stocks, type Stock } from './state';

const CURE_MS = 150_000;
const DROP_RADIUS = 7;

export function cure(dt: number): void {
    for (const [key, stock] of stocks) {
        if (!stock.curing.length) {
            continue;
        }

        let done = 0;

        for (const batch of stock.curing) {
            batch.left -= dt;

            if (batch.left > 0) {
                continue;
            }

            stock.cured.n += batch.n;
            stock.cured.q += batch.q;
            done += batch.n;
        }

        stock.curing = stock.curing.filter((batch) => batch.left > 0);
        changed();
        const player = done ? ownerOnline(key) : null;

        if (player) {
            toast(
                player,
                `${plural(done, 'bundle')} of tobacco cured in the barn. Stop a farm truck at the loading bay beside the barn to load ` +
                    `${stock.cured.n === 1 ? 'it' : 'them'}.`,
                { title: TITLE, tone: 'good', seconds: 7 },
            );
        }
    }
}

/** At the barn's arrow, the player's picked leaves go up to cure. */
export function hangAtBarn(player: Player, stock: Stock): void {
    const { signOn } = siteNow();

    if (
        !stock.leaves.n ||
        flatDistance(player.position, signOn) > DROP_RADIUS ||
        Math.abs(player.position.y - signOn.y) >= 4
    ) {
        return;
    }

    stock.curing.push({ n: stock.leaves.n, q: stock.leaves.q, left: CURE_MS });
    toast(
        player,
        `${plural(stock.leaves.n, 'bundle')} of leaves hung up in the barn to cure. Ready in ${formatMinutes(CURE_MS)}; ` +
            'it cures while you work.',
        { title: TITLE, tone: 'good' },
    );

    stock.leaves = { n: 0, q: 0 };
    changed();
}
