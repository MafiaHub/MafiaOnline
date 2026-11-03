// Going in and out: from the street door into the home's rooms, between the
// rooms of a villa, and back out; and showing out whoever may no longer stay.
import * as rooms from '../lib/house_rooms';
import { arrow, markers, toast, COLORS } from '../lib/notify';
import { MAIN_WORLD, type Placement } from '../lib/util';
import { title, type House } from './catalogue';
import { settle, stopCarrying, stopEditing } from './decorating';
import { spawnHome } from './furniture';
import { homeOf, keyOf, mayEnter, ownerOnline } from './homes';
import { blocked, inside, insideHouse, moves, occupants, roomOf } from './occupancy';

function doorMarkers(player: Player, roomKey: rooms.RoomKey): void {
    markers(
        player,
        'home-doors',
        rooms.ROOMS[roomKey].doors.map((door, index) =>
            arrow(
                `d${index}`,
                rooms.toWorld(roomKey, door.at),
                door.label,
                door.to === 'street' ? COLORS.green : COLORS.gold,
                14,
                { near: 10, map: false },
            ),
        ),
    );
}

export function enter(player: Player, house: House): void {
    const problem = !rooms.isBuilt()
        ? 'The door is stuck. Try again in a moment.'
        : blocked(player);

    if (problem) {
        toast(player, problem, { tone: 'bad', seconds: 3 });

        return;
    }

    const kind = rooms.KINDS[house.tier];

    moves.move(player, rooms.pose(kind.entry, rooms.ROOMS[kind.entry].arrive), house.world, {
        then: (moved) => {
            if (!moved) {
                return;
            }

            inside.set(player.id, { houseId: house.id, room: kind.entry });
            // Only once they are in: a transfer that never arrives leaves nothing behind.
            spawnHome(house);
            doorMarkers(player, kind.entry);
            welcome(player, house);
        },
    });
}

function welcome(player: Player, house: House): void {
    const home = homeOf(house);

    if (home?.owner === keyOf(player)) {
        toast(
            player,
            home.decor.length
                ? 'Home. F2 → Homes to decorate.'
                : 'Your new home is bare. F2 → Homes has furniture and pictures to buy.',
            { title: title(house), tone: 'good', seconds: 5 },
        );

        return;
    }

    toast(
        player,
        `${home?.ownerName ?? 'Someone'}'s ${rooms.KINDS[house.tier].title.toLowerCase()}.`,
        {
            title: title(house),
            seconds: 4,
        },
    );

    const owner = home && ownerOnline(home);

    if (owner && owner.id !== player.id) {
        toast(owner, `${player.nickname} came into your home.`, { seconds: 4 });
    }
}

/** Out to the street, in front of the door or at `to`. */
export function leave(player: Player, to: Placement | null = null): void {
    const house = insideHouse(player);

    if (!house) {
        return;
    }

    stopEditing(player);
    moves.move(player, to ?? house.out, MAIN_WORLD, {
        then: (moved) => {
            if (!moved) {
                return;
            }

            inside.delete(player.id);
            markers(player, 'home-doors', []);
            settle(house);
        },
    });
}

/** Through a door between two rooms of a villa. */
export function walkThrough(player: Player, house: House, door: rooms.RoomDoor): void {
    const fromKey = roomOf(player);

    if (door.to === 'street' || !fromKey) {
        return;
    }

    const to = door.to;
    const target = rooms.ROOMS[to];
    const back = target.returns?.[fromKey];

    stopCarrying(player);
    moves.move(player, rooms.pose(to, back ?? target.arrive), house.world, {
        then: (moved) => {
            if (!moved) {
                return;
            }

            inside.set(player.id, { houseId: house.id, room: to });
            doorMarkers(player, to);
        },
    });
}

/** Out of the rooms without a walk: a death, a disconnect, a mission change. */
export function release(player: Player): void {
    const house = insideHouse(player);

    stopEditing(player);
    inside.delete(player.id);
    moves.forget(player);
    markers(player, 'home-doors', []);

    if (house && player.virtualWorld === house.world) {
        player.setVirtualWorld(MAIN_WORLD);
    }

    settle(house);
}

/** Everyone inside who may no longer be there goes out. */
export function evict(house: House, reason: string): void {
    for (const visitor of occupants(house)) {
        if (moves.isMoving(visitor) || mayEnter(visitor, house)) {
            continue;
        }

        toast(visitor, reason, { seconds: 4 });
        leave(visitor);
    }
}

/** At a locked door: the owner, if they are about, hears it. */
export function knock(player: Player, house: House): void {
    const home = homeOf(house);
    const owner = home && ownerOnline(home);

    if (!owner) {
        toast(player, 'Nobody answers. The door is locked.', { seconds: 3 });

        return;
    }

    toast(player, `You knock. ${owner.nickname} lives here.`, { seconds: 3 });
    toast(
        owner,
        `${player.nickname} is knocking at ${title(house)}. /home invite ${player.nickname} lets them in.`,
        { seconds: 8 },
    );
}
