// Buying a home at its door (G, then G again to confirm) and selling it back
// (60% of its price, the furnishings at full value; there is no running cost).
import * as profiles from '../lib/profiles';
import * as rooms from '../lib/house_rooms';
import { feed, name, text, toast, COLORS } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { MAIN_WORLD, formatMoney, playable } from '../lib/util';
import { ALL, doorAt, here, title, type House } from './catalogue';
import { dropCarriedIn } from './decorating';
import { leave } from './entering';
import { despawnHome } from './furniture';
import {
    MAX_HOMES,
    homeOf,
    keyOf,
    ownedAnywhere,
    refundFor,
    removeHome,
    save,
    setHome,
} from './homes';
import { browse, publish, stopBrowsing } from './markers';
import { mine, occupants } from './occupancy';

const CONFIRM_MS = 8000;
const SELL_CONFIRM_MS = 15_000;

/** A second ask about the same home within `ms` goes ahead. */
class Confirmation {
    private readonly asked = new PlayerMap<{ houseId: string; at: number }>();

    constructor(private readonly ms: number) {}

    isAsked(player: Player, house: House): boolean {
        const ask = this.asked.get(player.id);

        return ask?.houseId === house.id && Date.now() - ask.at <= this.ms;
    }

    ask(player: Player, house: House): void {
        this.asked.set(player.id, { houseId: house.id, at: Date.now() });
    }

    done(player: Player): void {
        this.asked.delete(player.id);
    }
}

const buying = new Confirmation(CONFIRM_MS);
const selling = new Confirmation(SELL_CONFIRM_MS);

function buy(player: Player, house: House): void {
    const key = keyOf(player);

    // Only a player with a record (logged in) can own anything.
    if (!key) {
        return;
    }

    if (homeOf(house)) {
        toast(player, 'Somebody beat you to it: this one is sold.', { tone: 'bad' });

        return;
    }

    if (ownedAnywhere(player) >= MAX_HOMES) {
        toast(player, `You own ${MAX_HOMES} homes already. Sell one first (F2 → Homes).`, {
            tone: 'bad',
        });

        return;
    }

    if (!player.trySpendMoney(house.price)) {
        toast(
            player,
            `${title(house)} costs ${formatMoney(house.price)}; you have ${formatMoney(player.money)}.`,
            { tone: 'bad' },
        );

        return;
    }

    setHome(house, {
        owner: key,
        ownerName: player.nickname,
        paid: house.price,
        bought: Date.now(),
        locked: true,
        crew: null,
        guests: {},
        decor: [],
        next: 1,
    });

    save();
    profiles.bump(player, 'homesBought');
    toast(
        player,
        `Yours for ${formatMoney(house.price)}. G at the door lets you in; F2 → Homes to lock, invite and decorate.` +
            (house.parking ? ' Park a car of yours at the kerb outside with G.' : ''),
        { title: title(house), tone: 'gold', seconds: 9 },
    );

    if (house.tier !== 'poor') {
        feed('home', [
            name(player, COLORS.gold),
            text(` bought a ${house.tier === 'rich' ? house.style.toLowerCase() : 'flat'}`),
        ]);
    }

    stopBrowsing(player);
    publish(player);
}

function sell(player: Player, house: House): void {
    const home = homeOf(house);

    if (!home || home.owner !== keyOf(player)) {
        toast(player, 'That is not your home.', { tone: 'bad' });

        return;
    }

    for (const visitor of occupants(house)) {
        leave(visitor);
    }

    dropCarriedIn(house);
    const refund = refundFor(house, home);

    removeHome(house);
    save();
    despawnHome(house);
    player.giveMoney(refund);
    toast(
        player,
        `Sold back for ${formatMoney(refund)} (60% of the price, the furnishings at full value).`,
        { title: title(house), tone: 'good', seconds: 7 },
    );

    publish(player);
}

/** G at the door of a home for sale: the price, and a second G buys it. */
export function pressForSale(player: Player, house: House): void {
    if (buying.isAsked(player, house)) {
        buying.done(player);
        buy(player, house);

        return;
    }

    buying.ask(player, house);
    toast(
        player,
        `${title(house)}: ${rooms.KINDS[house.tier].title.toLowerCase()}, ${formatMoney(house.price)}. Press G again to buy.`,
        { title: 'For sale', seconds: 6 },
    );
}

export function forSaleHint(player: Player, house: House): string {
    return buying.isAsked(player, house)
        ? `G AGAIN · BUY FOR ${formatMoney(house.price)}`
        : `G · BUY · ${formatMoney(house.price)}`;
}

/** /home buy: at the door, without the second press. */
export function buyCommand(player: Player): void {
    const door =
        playable(player) && player.virtualWorld === MAIN_WORLD ? doorAt(player.position) : null;

    if (!door || homeOf(door)) {
        toast(player, 'Stand at the door of a home for sale (the gold arrow).', { tone: 'bad' });

        return;
    }

    buy(player, door);
}

/** /home show <id>: marks a home of this map for the player. */
export function showCommand(player: Player, id: string | undefined): void {
    const house = id === undefined ? undefined : ALL.get(id);

    if (!house || !here().includes(house)) {
        toast(player, 'No such home on this map.', { tone: 'bad' });

        return;
    }

    browse(player, house);
    toast(player, `${title(house)} is marked on the city map (TAB); your compass points there.`, {
        seconds: 5,
    });
}

/** /home sell [id]: asks first, then sells on a second ask within 15 seconds. */
export function sellCommand(player: Player, id: string | undefined): void {
    const house = mine(player, id);
    const home = house && homeOf(house);

    if (!house || !home) {
        return;
    }

    if (selling.isAsked(player, house)) {
        selling.done(player);
        sell(player, house);

        return;
    }

    selling.ask(player, house);
    toast(
        player,
        `Sell ${title(house)} for ${formatMoney(refundFor(house, home))}? Press Sell again (or /home sell ${house.id}) within 15 seconds.`,
        { tone: 'bad', seconds: 8 },
    );
}
