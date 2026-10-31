// The decorating editor: the owner buys pieces from the catalogue
// (lib/house_decor.ts), walks them into place and puts them down with G, and
// picks up, turns, raises and sells back what is already there.
import * as log from '../lib/log';
import * as rooms from '../lib/house_rooms';
import * as decor from '../lib/house_decor';
import { every } from '../lib/lifecycle';
import { toast } from '../lib/notify';
import { fields } from '../lib/payload';
import { PlayerMap, PlayerSet } from '../lib/players';
import { emit, flatDistance, formatMoney, headingOf, onClient, playerById } from '../lib/util';
import { ALL, type House } from './catalogue';
import { createDecor, decorPose, despawnHome, liftOut, spawnHome, standIn } from './furniture';
import { homeOf, isOwner, save, type DecorEntry } from './homes';
import { insideHouse, occupants, roomOf } from './occupancy';
import type { Point } from '../lib/util';

const CARRY_TICK_MS = 100;
const LIFT_STEP = 0.1;
const TURN_STEP = Math.PI / 12;
// How far in front of the player a carried piece floats, beyond its own half-size.
const CARRY_AHEAD = 0.9;
// How close to the probe in front of the player a piece is picked up.
const PICK_REACH = 1.8;
// How close a solid piece may come to where people arrive and to the door arrows.
const DOORWAY_CLEAR = 1.2;
// The decorating keys (client/housing.ts), off the keys the game plays with:
// Home/End (or numpad 4/6) turn, PgUp/PgDn (or numpad 8/2) raise and lower,
// Delete sells back, numpad 0 puts back or ends decorating.
const KEYS_HINT =
    'Home/End turn it, PgUp/PgDn raise and lower it, Delete sells it back, Num 0 puts it back';

interface Carried {
    houseId: string;
    /** The decoration's row; a placed piece's is the home's own, moved as it is carried. */
    entry: DecorEntry;
    item: decor.DecorItem;
    /** A placed piece's row as it was, to put it back. */
    previous: DecorEntry | null;
    isNew: boolean;
    /** The model following the player, without collision. */
    object: MapObject | null;
}

const editing = new PlayerSet();
const carrying = new PlayerMap<Carried>();
const failures = new log.Once();

export function isEditing(player: Player): boolean {
    return editing.has(player.id);
}

export function carriedBy(player: Player): Carried | null {
    return carrying.get(player.id) ?? null;
}

/** After someone leaves: an empty home packs its decorations away. */
export function settle(house: House | null): void {
    if (
        house &&
        !occupants(house).length &&
        ![...carrying.values()].some((entry) => entry.houseId === house.id)
    ) {
        despawnHome(house);
    }
}

/** The home is gone: nobody carries its pieces any more. */
export function dropCarriedIn(house: House): void {
    for (const [id, entry] of carrying) {
        if (entry.houseId === house.id) {
            carrying.delete(id);
        }
    }
}

/** The mission change took every object with it. */
export function forgetAll(): void {
    carrying.clear();
    editing.clear();
}

export function startEditing(player: Player): void {
    editing.add(player.id);
    emit(player, 'fr:home:edit', { on: true });
}

export function stopEditing(player: Player): void {
    stopCarrying(player);

    if (editing.delete(player.id)) {
        emit(player, 'fr:home:edit', { on: false });
    }
}

function forward(player: Player): { heading: number; x: number; z: number } {
    // A player's rotation reads as a quaternion.
    const heading = headingOf(player.rotation as Quaternion);

    return { heading, x: Math.sin(heading), z: Math.cos(heading) };
}

function clamp(value: number, low: number, high: number): number {
    return low > high ? (low + high) / 2 : Math.min(high, Math.max(low, value));
}

// The carried piece, if any, follows the player: in front of them on the
// floor (raised by its lift), turned by its yaw, kept inside the walls.
function carryTarget(
    player: Player,
    entry: DecorEntry,
    item: decor.DecorItem,
): { position: Point; rotation: Point } | null {
    const roomKey = roomOf(player);

    if (!roomKey) {
        return null;
    }

    const room = rooms.ROOMS[roomKey];
    const [x0, x1, y0, y1, z0, z1] = item.box;
    const hx = (x1 - x0) / 2;
    const hz = (z1 - z0) / 2;
    const cos = Math.abs(Math.cos(entry.yaw));
    const sin = Math.abs(Math.sin(entry.yaw));
    const ex = cos * hx + sin * hz;
    const ez = sin * hx + cos * hz;
    const ahead = CARRY_AHEAD + Math.max(ex, ez);
    const local = rooms.toLocal(roomKey, player.position);
    const dir = forward(player);
    const { bounds, walls } = room;
    // Pictures, mirrors and trophies lie flat against a wall; everything else keeps clear of it.
    const limit = decor.WALL.has(item.id)
        ? { x0: walls.x0 + 0.01, x1: walls.x1 - 0.01, z0: walls.z0 + 0.01, z1: walls.z1 - 0.01 }
        : bounds;

    entry.room = roomKey;
    entry.x = clamp(local.x + dir.x * ahead, limit.x0 + ex, limit.x1 - ex);
    entry.z = clamp(local.z + dir.z * ahead, limit.z0 + ez, limit.z1 - ez);
    entry.lift =
        Math.round(
            clamp(entry.lift, 0, Math.max(0, bounds.ceiling - bounds.floor - (y1 - y0) - 0.05)) *
                100,
        ) / 100;

    return decorPose(entry, item);
}

function pickUp(
    player: Player,
    house: House,
    entry: DecorEntry,
    item: decor.DecorItem,
    isNew: boolean,
): void {
    liftOut(house, entry);
    carrying.set(player.id, {
        houseId: house.id,
        entry,
        item,
        previous: isNew ? null : { ...entry },
        isNew,
        object: createDecor(entry, house.world, false),
    });
}

function blocksDoorway(entry: DecorEntry, item: decor.DecorItem): boolean {
    if (decor.PASSABLE.has(item.id)) {
        return false;
    }

    const room = rooms.ROOMS[entry.room];
    const [x0, x1, , , z0, z1] = item.box;
    const half = Math.max(x1 - x0, z1 - z0) / 2;

    return [room.arrive.position, ...room.doors.map((door) => door.at)].some(
        (point) => Math.hypot(point.x - entry.x, point.z - entry.z) - half < DOORWAY_CLEAR,
    );
}

/** Puts the carried piece down where it is; a new one is paid for now. */
export function place(player: Player): void {
    const carried = carrying.get(player.id);

    if (!carried) {
        return;
    }

    const house = ALL.get(carried.houseId);
    const home = house ? homeOf(house) : null;

    if (!house || !home) {
        carrying.delete(player.id);
        carried.object?.destroy();

        return;
    }

    // Where it follows the player now, within the walls and under the ceiling.
    if (insideHouse(player)?.id === house.id) {
        carryTarget(player, carried.entry, carried.item);
    }

    if (blocksDoorway(carried.entry, carried.item)) {
        toast(player, 'Keep the doorway clear: put it further into the room.', {
            tone: 'bad',
            seconds: 3,
        });

        return;
    }

    if (carried.isNew && !player.trySpendMoney(carried.item.price)) {
        toast(
            player,
            `${carried.item.name}: ${formatMoney(carried.item.price)}. You have ${formatMoney(player.money)}.`,
            { tone: 'bad' },
        );

        return;
    }

    carrying.delete(player.id);
    carried.object?.destroy();

    if (carried.isNew) {
        home.decor.push(carried.entry);
    }

    save();
    standIn(house, carried.entry);
    toast(player, `${carried.item.name} placed.`, { seconds: 2 });
}

/** Puts a carried piece back where it was; a new one goes back to the shop unpaid. */
export function stopCarrying(player: Player): void {
    const carried = carrying.get(player.id);

    if (!carried) {
        return;
    }

    carrying.delete(player.id);
    carried.object?.destroy();
    const house = ALL.get(carried.houseId);

    // A new piece is paid for only when it is put down.
    if (!house || !homeOf(house) || carried.isNew) {
        return;
    }

    if (carried.previous) {
        Object.assign(carried.entry, carried.previous);
    }

    standIn(house, carried.entry);
}

function sellCarried(player: Player, carried: Carried): void {
    carrying.delete(player.id);
    carried.object?.destroy();

    if (carried.isNew) {
        toast(player, `${carried.item.name} left in the shop.`, { seconds: 2 });

        return;
    }

    const house = ALL.get(carried.houseId);
    const home = house ? homeOf(house) : null;

    if (home) {
        home.decor = home.decor.filter((entry) => entry !== carried.entry);
    }

    save();
    player.giveMoney(carried.item.price);
    toast(player, `${carried.item.name} sold back for ${formatMoney(carried.item.price)}.`, {
        tone: 'good',
        seconds: 3,
    });
}

/** The piece in front of the player in their room that G picks up, with its catalogue item. */
function nearestDecor(
    player: Player,
    house: House,
): { entry: DecorEntry; item: decor.DecorItem } | null {
    const room = roomOf(player);
    const dir = forward(player);
    const probe = {
        x: player.position.x + dir.x * CARRY_AHEAD,
        y: player.position.y,
        z: player.position.z + dir.z * CARRY_AHEAD,
    };
    let best: { entry: DecorEntry; item: decor.DecorItem; gap: number } | null = null;

    for (const entry of homeOf(house)?.decor ?? []) {
        const item = decor.byId(entry.item);

        if (entry.room !== room || !item) {
            continue;
        }

        const gap = flatDistance(
            probe,
            rooms.toWorld(entry.room, { x: entry.x, y: 0, z: entry.z }),
        );

        if (gap <= PICK_REACH && (!best || gap < best.gap)) {
            best = { entry, item, gap };
        }
    }

    return best;
}

/** G while decorating with nothing in hand. */
export function pickUpInFront(player: Player, house: House): void {
    const found = nearestDecor(player, house);

    if (found) {
        pickUp(player, house, found.entry, found.item, false);
    } else {
        toast(player, 'Nothing to pick up there. Stand in front of a piece you bought.', {
            seconds: 3,
        });
    }
}

function buyDecor(player: Player, house: House, item: decor.DecorItem): void {
    const home = homeOf(house);
    const room = roomOf(player);

    if (!home || !room) {
        return;
    }

    if (!decor.sells(house.tier, item)) {
        toast(player, `A ${item.name.toLowerCase()} belongs in a grander home.`, { tone: 'bad' });

        return;
    }

    if (home.decor.length >= decor.LIMITS[house.tier]) {
        toast(
            player,
            `This home holds ${decor.LIMITS[house.tier]} pieces. Sell one back first (pick it up, Delete).`,
            { tone: 'bad' },
        );

        return;
    }

    stopCarrying(player);

    if (player.money < item.price) {
        toast(
            player,
            `${item.name}: ${formatMoney(item.price)}. You have ${formatMoney(player.money)}.`,
            { tone: 'bad' },
        );

        return;
    }

    const entry: DecorEntry = {
        n: home.next++,
        item: item.id,
        room,
        x: 0,
        z: 0,
        lift: 0,
        yaw: forward(player).heading,
    };

    if (!editing.has(player.id)) {
        startEditing(player);
    }

    spawnHome(house);
    pickUp(player, house, entry, item, true);
    toast(
        player,
        `${item.name}, ${formatMoney(item.price)} when you put it down. Walk it into place and press G. ${KEYS_HINT}.`,
        { seconds: 8 },
    );
}

every(CARRY_TICK_MS, () => {
    for (const [id, carried] of carrying) {
        try {
            const player = playerById(id);

            if (!player || insideHouse(player)?.id !== carried.houseId || !carried.object) {
                continue;
            }

            const pose = carryTarget(player, carried.entry, carried.item);

            if (pose) {
                carried.object.moveTo(pose.position, 6, pose.rotation);
            }
        } catch (caught) {
            failures.error('Carrying a home piece failed', caught);
        }
    }
});

function editKey(player: Player, request: unknown): void {
    const carried = carrying.get(player.id);

    if (!editing.has(player.id) || !insideHouse(player)) {
        return;
    }

    const { sell, cancel, turn, lift } = fields(request);

    if (sell) {
        if (carried) {
            sellCarried(player, carried);
        }

        return;
    }

    if (cancel) {
        if (carried) {
            stopCarrying(player);
        } else {
            stopEditing(player);
            toast(player, 'Done decorating.', { seconds: 2 });
        }

        return;
    }

    if (!carried) {
        return;
    }

    const turns = Math.sign(Number(turn) || 0);
    const lifts = Math.sign(Number(lift) || 0);

    if (turns) {
        carried.entry.yaw = (carried.entry.yaw + turns * TURN_STEP) % (2 * Math.PI);
    }

    if (lifts) {
        carried.entry.lift = Math.round((carried.entry.lift + lifts * LIFT_STEP) * 100) / 100;
    }

    // Kept between the floor and the ceiling at once.
    carryTarget(player, carried.entry, carried.item);
}

onClient('fr:home:edit', (player, request: unknown) => {
    try {
        editKey(player, request);
    } catch (caught) {
        log.error(`Decorating key failed for ${player.nickname}: ${log.describe(caught, true)}`);
    }
});

/** /home edit: starts or ends decorating. */
export function editCommand(player: Player): void {
    const house = insideHouse(player);

    if (!house || !isOwner(player, house)) {
        toast(player, 'Decorate inside your own home.', { tone: 'bad' });

        return;
    }

    if (editing.has(player.id)) {
        stopEditing(player);
        toast(player, 'Done decorating.', { seconds: 2 });

        return;
    }

    startEditing(player);
    toast(
        player,
        'Decorating: face a piece you bought and press G to pick it up. Num 0 when done.',
        { seconds: 6 },
    );
}

/** /home decor <item>: buys a piece, carried until it is put down. */
export function decorCommand(player: Player, itemId: string | undefined): void {
    const house = insideHouse(player);
    const item = decor.byId(itemId);

    if (!house || !isOwner(player, house)) {
        toast(player, 'Buy furnishings inside your own home.', { tone: 'bad' });

        return;
    }

    if (!item) {
        toast(player, 'No such piece.', { tone: 'bad' });

        return;
    }

    buyDecor(player, house, item);
}
