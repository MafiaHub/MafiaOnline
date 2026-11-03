// The Homes tab of the F2 city menu: the player's homes, the ones they may
// visit, the ones for sale nearest first, and the home they stand in with
// its catalogue.
import * as decor from '../lib/house_decor';
import * as menus from '../lib/menu';
import * as rooms from '../lib/house_rooms';
import * as dealership from '../dealership';
import { flatDistance } from '../lib/util';
import { here, title, type House } from './catalogue';
import { isEditing } from './decorating';
import {
    MAX_HOMES,
    homeOf,
    isOwner,
    keyOf,
    mayEnter,
    ownedAnywhere,
    ownedBy,
    refundFor,
} from './homes';
import { browsingId } from './markers';
import { insideHouse } from './occupancy';
import type { HouseTier } from '../data/types';

const TIERS: readonly HouseTier[] = ['poor', 'middle', 'rich'];
const FOR_SALE_SHOWN = 12;

// Distances count from the street: from the door of the home one is in.
function view(player: Player, house: House) {
    const from = insideHouse(player)?.door ?? player.position;

    return {
        id: house.id,
        name: title(house),
        kind: rooms.KINDS[house.tier].title,
        tier: house.tier,
        price: house.price,
        distance: Math.round(flatDistance(from, house.door)),
        parking: Boolean(house.parking),
        owner: homeOf(house)?.ownerName ?? null,
    };
}

function mineList(player: Player, current: House | null) {
    return ownedBy(player).flatMap((house) => {
        const home = homeOf(house);

        return home
            ? [
                  {
                      ...view(player, house),
                      locked: home.locked,
                      crew: home.crew,
                      guests: Object.values(home.guests),
                      decor: home.decor.length,
                      limit: decor.LIMITS[house.tier],
                      sellFor: refundFor(house, home),
                      cars: house.parking
                          ? dealership
                                .parkedAt(player, house.id)
                                .map((car) => ({ slot: car.slot + 1, model: car.model }))
                          : [],
                      here: current?.id === house.id,
                  },
              ]
            : [];
    });
}

function insideView(player: Player, current: House) {
    const own = isOwner(player, current);

    return {
        id: current.id,
        name: title(current),
        mine: own,
        owner: homeOf(current)?.ownerName ?? null,
        editing: isEditing(player),
        decor: homeOf(current)?.decor.length ?? 0,
        limit: decor.LIMITS[current.tier],
        catalogue: own
            ? decor.forTier(current.tier).map((item) => ({
                  id: item.id,
                  name: item.name,
                  price: item.price,
                  category: item.category,
              }))
            : [],
    };
}

menus.provide(
    'homes',
    (player) => {
        const key = keyOf(player);
        const list = here();
        const current = insideHouse(player);
        const visiting = list
            .filter((house) => {
                const home = homeOf(house);

                return home && home.owner !== key && mayEnter(player, house);
            })
            .map((house) => view(player, house));
        const forSale = list
            .filter((house) => !homeOf(house))
            .map((house) => view(player, house))
            .sort((a, b) => a.distance - b.distance);
        const counts: Record<HouseTier, number> = { poor: 0, middle: 0, rich: 0 };

        for (const house of forSale) {
            counts[house.tier]++;
        }

        return {
            max: MAX_HOMES,
            owned: ownedAnywhere(player),
            mine: mineList(player, current),
            visiting,
            forSale: forSale.slice(0, FOR_SALE_SHOWN),
            counts,
            total: list.length,
            browsing: browsingId(player),
            inside: current ? insideView(player, current) : null,
            prices: Object.fromEntries(
                TIERS.map((tier) => {
                    const prices = list
                        .filter((house) => house.tier === tier)
                        .map((house) => house.price);

                    return [
                        tier,
                        prices.length ? [Math.min(...prices), Math.max(...prices)] : null,
                    ];
                }),
            ),
        };
    },
    ['homes'],
);
