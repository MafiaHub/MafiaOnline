// Hold-ups. A player with a firearm at a shop counter can /rob it: the
// police are called at once, and the till is theirs if they keep the counter
// for fifteen seconds. Each shop needs ten minutes to restock its till.
import * as life from './lib/life';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { SHOPS } from './shops';
import * as maps from './lib/maps';
import { toast, card, name, text, COLORS } from './lib/notify';
import { distance, formatMoney, formatMinutes, onStreet, playable, playerById } from './lib/util';
import { events, every } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { Countdown } from './lib/errands';
import { reward } from './lib/rewards';

const COUNTER_REACH = 3.5;
const HOLD_REACH = 7;
const HOLD_MS = 15_000;
const RESTOCK_MS = 10 * 60_000;
const MIN_TAKE = 250;
const MAX_TAKE = 450;
const XP = 60;
const TICK_MS = 500;

type Shop = (typeof SHOPS)[number];

interface Holdup {
    shop: Shop;
    hold: Countdown;
}

// Shop id -> when its till is full again.
const restockedAt = new Map<string, number>();
const holdups = new PlayerMap<Holdup>();

function armed(player: Player): boolean {
    const weapon = player.getCurrentWeapon();

    return weapon !== 0 && Weapon.get(weapon)?.kind === 'firearm';
}

function stop(player: Player, message: string | null): void {
    const holdup = holdups.get(player.id);

    if (!holdup) {
        return;
    }

    holdups.delete(player.id);
    life.end(player, 'robbery');
    card(player, 'robbery', null);

    if (message) {
        toast(player, message, { title: holdup.shop.name, tone: 'bad' });
    }
}

function tick(): void {
    const now = Date.now();

    for (const [id, holdup] of holdups) {
        const player = playerById(id);

        if (!player) {
            holdups.delete(id);
            continue;
        }

        if (!playable(player) || distance(player.position, holdup.shop.position) > HOLD_REACH) {
            stop(player, 'You left the counter. The till stays shut.');
            continue;
        }

        if (!holdup.hold.over(now)) {
            card(player, 'robbery', {
                title: `Hold-up · ${holdup.shop.name}`,
                subtitle: 'Keep the counter. The police are coming.',
                rows: [['Till open in', formatMinutes(holdup.hold.left(now))]],
                progress: holdup.hold.progress(now),
                tone: 'bad',
            });

            continue;
        }

        const take = MIN_TAKE + Math.floor(Math.random() * (MAX_TAKE - MIN_TAKE + 1));

        stop(player, null);
        reward(player, {
            money: take,
            counters: ['robberies'],
            xp: XP,
            reason: 'Hold-up',
            feed: {
                kind: 'police',
                parts: [
                    name(player, COLORS.red),
                    text(` robbed ${holdup.shop.name} · `),
                    text(formatMoney(take), COLORS.gold),
                ],
            },
        });

        Police.setWanted(player, 3);
        toast(player, `${formatMoney(take)} from the till. Now run.`, {
            title: holdup.shop.name,
            tone: 'gold',
        });
    }
}

commands.register(
    {
        name: 'rob',
        usage: '/rob',
        help: 'Hold up the shop counter you are standing at.',
        category: 'Combat',
    },
    (player) => {
        if (!onStreet(player) || player.getVehicle()) {
            return;
        }

        const shop = maps.current()?.shops
            ? SHOPS.find((entry) => distance(player.position, entry.position) <= COUNTER_REACH)
            : undefined;

        if (!shop) {
            toast(player, 'Stand at a shop counter first.', { tone: 'bad' });

            return;
        }

        if (!armed(player)) {
            toast(player, 'Nobody hands over the till to an empty hand. Draw a gun.', {
                tone: 'bad',
            });

            return;
        }

        const wait = (restockedAt.get(shop.id) ?? 0) - Date.now();

        if (wait > 0) {
            toast(player, `The till is empty. ${shop.name} restocks in ${formatMinutes(wait)}.`, {
                tone: 'bad',
            });

            return;
        }

        if (!life.begin(player, 'robbery', `Robbing ${shop.name}`)) {
            toast(player, life.busyMessage(player) ?? 'You are busy.', { tone: 'bad' });

            return;
        }

        restockedAt.set(shop.id, Date.now() + RESTOCK_MS);
        holdups.set(player.id, { shop, hold: new Countdown(HOLD_MS) });
        Police.setWanted(player, 2);
        toast(player, 'Hands where I can see them! Hold the counter for fifteen seconds.', {
            title: shop.name,
            tone: 'bad',
        });
    },
);

menu.provide('robbery', () =>
    SHOPS.map((shop) => ({
        name: shop.name,
        readyInSeconds: Math.max(
            0,
            Math.ceil(((restockedAt.get(shop.id) ?? 0) - Date.now()) / 1000),
        ),
    })),
);

every(TICK_MS, tick);
events.on('playerDeath', (player) => stop(player, 'The hold-up ended with you on the floor.'));
