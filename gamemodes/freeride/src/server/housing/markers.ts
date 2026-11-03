// What the street shows of the homes: an arrow over the owner's door (and
// their parking spot), over the homes they may visit, and over the doors for
// sale nearby; and a home picked from the list (/home show), marked on the
// city map with the compass pointing at it for a while.
import { arrow, compass, markers, COLORS, type Marker } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { flatDistance, formatMoney } from '../lib/util';
import { ALL, KIND_WORD, here, type House } from './catalogue';
import { homeOf, keyOf, mayEnter } from './homes';
import { insideHouse } from './occupancy';

const MARKER_RANGE = 70;
const BROWSE_MS = 180_000;
// A browsed home stops being marked this close to its door.
const BROWSE_ARRIVED = 12;
const COMPASS_PRIORITY = 18;

interface Browse {
    houseId: string;
    until: number;
}

const browsing = new PlayerMap<Browse>();

function streetMarkers(player: Player): Marker[] {
    const key = keyOf(player);
    const items: Marker[] = [];
    const browse = browsing.get(player.id);

    for (const house of here()) {
        const home = homeOf(house);
        const near = flatDistance(player.position, house.door) <= MARKER_RANGE;

        if (home?.owner === key) {
            items.push(
                arrow(house.id, house.door, 'HOME', COLORS.green, 60, {
                    near: 30,
                    map: { icon: 'square', label: 'Home', size: 5 },
                }),
            );

            if (house.parking && near) {
                items.push({
                    id: `${house.id}/p`,
                    position: house.parking.position,
                    label: 'PARKING',
                    color: COLORS.blue,
                    range: 35,
                    near: 18,
                    map: false,
                });
            }
        } else if (home && mayEnter(player, house)) {
            items.push(
                arrow(
                    house.id,
                    house.door,
                    `${String(home.ownerName).toUpperCase()}'S PLACE`,
                    COLORS.blue,
                    50,
                    {
                        near: 25,
                        map: home.locked
                            ? { icon: 'square', label: `${home.ownerName}'s place`, size: 4 }
                            : false,
                    },
                ),
            );
        } else if (!home && near) {
            items.push(
                arrow(
                    house.id,
                    house.door,
                    `FOR SALE · ${KIND_WORD[house.tier]} · ${formatMoney(house.price)}`,
                    COLORS.gold,
                    40,
                    { near: 25, map: false },
                ),
            );
        }

        if (browse?.houseId === house.id && !home) {
            items.push({
                id: `${house.id}/b`,
                position: house.door,
                color: COLORS.gold,
                map: {
                    icon: 'diamond',
                    label: `For sale ${formatMoney(house.price)}`,
                    size: 6,
                    edge: true,
                },
            });
        }
    }

    return items;
}

/** The player's street markers; none while they are inside a home. */
export function publish(player: Player): void {
    if (!insideHouse(player)) {
        markers(player, 'homes', streetMarkers(player));
    }
}

/** /home show: marks the home for sale and points the compass at it. */
export function browse(player: Player, house: House): void {
    browsing.set(player.id, { houseId: house.id, until: Date.now() + BROWSE_MS });
    compass(player, 'home', COMPASS_PRIORITY, { position: house.door });
    publish(player);
}

export function stopBrowsing(player: Player): void {
    browsing.delete(player.id);
    compass(player, 'home', COMPASS_PRIORITY, null);
}

export function browsingId(player: Player): string | null {
    return browsing.get(player.id)?.houseId ?? null;
}

/** A browsed home stops being marked after a while, or once the player is there. */
export function tickBrowse(player: Player, now: number): void {
    const browse = browsing.get(player.id);
    const house = browse ? ALL.get(browse.houseId) : null;

    if (
        browse &&
        (now > browse.until ||
            (house && flatDistance(player.position, house.door) < BROWSE_ARRIVED))
    ) {
        stopBrowsing(player);
    }
}
