// The job key (G) at the homes, and its prompt line: at a street door it
// buys (press twice), goes in or knocks; inside it goes through a door, puts
// down the piece being carried or, decorating, picks one up; in a garage car
// on the owner's parking spot it parks it, and on foot there it takes a
// parked car out. Also the watch on everyone in a home, a few times a second.
import * as life from '../lib/life';
import * as log from '../lib/log';
import * as rooms from '../lib/house_rooms';
import { Interactions } from '../lib/interactions';
import { every } from '../lib/lifecycle';
import { toast } from '../lib/notify';
import { MAIN_WORLD, distance, formatMoney, playable } from '../lib/util';
import { doorAt, type House } from './catalogue';
import { carriedBy, isEditing, pickUpInFront, place } from './decorating';
import { enter, knock, leave, release, walkThrough } from './entering';
import { homeOf, isOwner, mayEnter } from './homes';
import { publish, tickBrowse } from './markers';
import { forSaleHint, pressForSale } from './market';
import { insideHouse, moves, roomOf } from './occupancy';
import {
    PARK_REACH,
    TAKE_OUT_REACH,
    hasParked,
    inOwnCar,
    parkHere,
    parkingNear,
    takeOut,
} from './parking';

const TICK_MS = 250;
// Street markers go out every fourth tick.
const MARKER_EVERY = 4;
const INSIDE_REACH = 1.6;
// How far below a room's floor someone has fallen through it.
const FALL_GUARD = 6;

type HomeAction =
    | { kind: 'place' }
    | { kind: 'door'; house: House; door: rooms.RoomDoor }
    | { kind: 'pickUp'; house: House }
    | { kind: 'park'; house: House }
    | { kind: 'buy'; house: House }
    | { kind: 'enter'; house: House }
    | { kind: 'knock'; house: House }
    | { kind: 'takeOut'; house: House };

const failures = new log.Once();
let ticks = 0;

function roomDoor(player: Player): rooms.RoomDoor | null {
    const roomKey = roomOf(player);

    if (!roomKey) {
        return null;
    }

    return (
        rooms.ROOMS[roomKey].doors.find(
            (door) => distance(player.position, rooms.toWorld(roomKey, door.at)) <= INSIDE_REACH,
        ) ?? null
    );
}

function chooseInside(player: Player, house: House): HomeAction | null {
    if (carriedBy(player)) {
        return { kind: 'place' };
    }

    const door = roomDoor(player);

    if (door) {
        return { kind: 'door', house, door };
    }

    return isEditing(player) && isOwner(player, house) ? { kind: 'pickUp', house } : null;
}

function chooseOutside(player: Player): HomeAction | null {
    if (player.getVehicle()) {
        const spot = parkingNear(player, PARK_REACH);

        return spot && player.getSeat() === 0 ? { kind: 'park', house: spot } : null;
    }

    const door = doorAt(player.position);

    if (door) {
        if (!homeOf(door)) {
            return { kind: 'buy', house: door };
        }

        return { kind: mayEnter(player, door) ? 'enter' : 'knock', house: door };
    }

    const spot = parkingNear(player, TAKE_OUT_REACH);

    return spot && hasParked(player, spot) ? { kind: 'takeOut', house: spot } : null;
}

function choose(player: Player): HomeAction | null {
    if (!playable(player) || moves.isMoving(player)) {
        return null;
    }

    const house = insideHouse(player);

    if (house) {
        return chooseInside(player, house);
    }

    return player.virtualWorld === MAIN_WORLD ? chooseOutside(player) : null;
}

function hint(player: Player, action: HomeAction): string | null {
    switch (action.kind) {
        case 'place': {
            const carried = carriedBy(player);

            return carried?.isNew
                ? `G buy ${formatMoney(carried.item.price)} · Home/End turn · PgUp/PgDn raise · Num0 back`
                : 'G place · Home/End turn · PgUp/PgDn raise · Del sell';
        }

        case 'door':
            return action.door.to === 'street' ? 'G · STEP OUTSIDE' : `G · ${action.door.label}`;
        case 'pickUp':
            return 'Decorating · G picks up a piece · Num 0 when done';
        case 'park':
            return inOwnCar(player) ? 'G · PARK IT HERE' : null;
        case 'buy':
            return forSaleHint(player, action.house);
        case 'enter':
            return 'G · GO IN';
        case 'knock':
            return 'G · KNOCK';
        case 'takeOut':
            return 'G · TAKE OUT YOUR CAR';
    }
}

function press(player: Player, action: HomeAction): void {
    switch (action.kind) {
        case 'place':
            place(player);
            break;
        case 'door':
            if (action.door.to === 'street') {
                leave(player);
            } else {
                walkThrough(player, action.house, action.door);
            }

            break;
        case 'pickUp':
            pickUpInFront(player, action.house);
            break;
        case 'park':
            parkHere(player, action.house);
            break;
        case 'buy':
            pressForSale(player, action.house);
            break;
        case 'enter':
            enter(player, action.house);
            break;
        case 'knock':
            knock(player, action.house);
            break;
        case 'takeOut':
            takeOut(player, action.house);
            break;
    }
}

// A key press or a check that fails is logged, never thrown into the key
// handler or the timer.
const interactions = new Interactions<HomeAction>({
    layer: 'home',
    enabled: () => true,
    choose: (player) => {
        try {
            return choose(player);
        } catch (caught) {
            failures.error(`Homes check failed for ${player.nickname}`, caught);

            return null;
        }
    },
    hint,
    press: (player, action) => {
        try {
            press(player, action);
        } catch (caught) {
            log.error(`Homes key failed for ${player.nickname}: ${log.describe(caught, true)}`);
        }
    },
    holds: {},
    finish: () => {},
});

function tickPlayer(player: Player, now: number): void {
    let house = insideHouse(player);
    const moving = moves.isMoving(player);

    // Another module moved them out (the jail, a blackout): the home no longer holds them.
    if (house && player.virtualWorld !== house.world && !moving) {
        release(player);
        house = null;
    }

    if (house && !moving && !mayEnter(player, house)) {
        toast(player, 'You are not welcome here any more.', { seconds: 4 });
        leave(player);

        return;
    }

    const roomKey = house ? roomOf(player) : null;

    if (roomKey && player.alive && !moving) {
        const room = rooms.ROOMS[roomKey];
        const floor = rooms.toWorld(roomKey, { x: 0, y: room.bounds.floor, z: 0 }).y;

        if (player.position.y < floor - FALL_GUARD) {
            life.teleport(player, rooms.pose(roomKey, room.arrive).position, 0);
        }
    }

    tickBrowse(player, now);

    if (ticks % MARKER_EVERY === 0 && playable(player)) {
        publish(player);
    }
}

// One player's trouble never stops the others' homes, or the resource.
every(TICK_MS, () => {
    ticks++;
    const now = Date.now();

    for (const player of World.getPlayers()) {
        try {
            tickPlayer(player, now);
        } catch (caught) {
            failures.error(`Homes tick failed for ${player.nickname}`, caught);
        }
    }

    interactions.showHints();
});
