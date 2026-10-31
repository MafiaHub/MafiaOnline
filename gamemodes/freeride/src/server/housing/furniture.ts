// A home's own decorations, standing in its virtual world while someone is
// there, and the fire in the villa's dining-room hearth.
import * as log from '../lib/log';
import * as rooms from '../lib/house_rooms';
import * as decor from '../lib/house_decor';
import { homeOf, type DecorEntry } from './homes';
import type { House } from './catalogue';
import type { Point } from '../lib/util';

interface Furnished {
    /** By decoration number. */
    objects: Map<number, MapObject>;
    fire: Fire | null;
}

// By the home's world.
const spawned = new Map<number, Furnished>();

/**
 * Where a decoration's model stands: its origin follows from the floor point
 * under its box centre, turned as lib/house_rooms.ts turns pieces.
 */
export function decorPose(
    entry: DecorEntry,
    item: decor.DecorItem,
): { position: Point; rotation: Point } {
    const room = rooms.ROOMS[entry.room];
    const [x0, x1, y0, , z0, z1] = item.box;
    const cx = (x0 + x1) / 2;
    const cz = (z0 + z1) / 2;
    const { yaw } = entry;
    const wx = cx * Math.cos(yaw) + cz * Math.sin(yaw);
    const wz = -cx * Math.sin(yaw) + cz * Math.cos(yaw);
    const local = { x: entry.x - wx, y: room.bounds.floor + entry.lift - y0, z: entry.z - wz };

    return { position: rooms.toWorld(entry.room, local), rotation: { x: 0, y: yaw, z: 0 } };
}

/** The decoration's model in the world; without collision while it is carried. */
export function createDecor(
    entry: DecorEntry,
    world: number,
    collision: boolean,
): MapObject | null {
    const item = decor.byId(entry.item);

    if (!item) {
        return null;
    }

    const { position, rotation } = decorPose(entry, item);

    return rooms.create(item.model, position, rotation, {
        virtualWorld: world,
        collision: collision && !decor.PASSABLE.has(item.id),
    });
}

export function spawnHome(house: House): void {
    if (spawned.has(house.world) || typeof MapObject === 'undefined') {
        return;
    }

    const record: Furnished = { objects: new Map(), fire: null };

    spawned.set(house.world, record);

    for (const entry of homeOf(house)?.decor ?? []) {
        const object = createDecor(entry, house.world, true);

        if (object) {
            record.objects.set(entry.n, object);
        }
    }

    const fire = rooms.KINDS[house.tier].rooms
        .map((key) => {
            const hearth = rooms.ROOMS[key].fire;

            return hearth ? rooms.toWorld(key, hearth) : null;
        })
        .find(Boolean);

    if (fire && typeof Fire !== 'undefined') {
        try {
            record.fire = Fire.create(fire, {
                radius: 0.35,
                height: 0,
                intensity: 0.3,
                damage: 0,
                virtualWorld: house.world,
            });
        } catch (caught) {
            log.warn(`The hearth of ${house.id} did not light: ${log.describe(caught)}`);
            record.fire = null;
        }
    }
}

export function despawnHome(house: House): void {
    const record = spawned.get(house.world);

    if (!record) {
        return;
    }

    for (const object of record.objects.values()) {
        object.destroy();
    }

    record.fire?.destroy();
    spawned.delete(house.world);
}

/** The mission change took every object with it. */
export function forgetAll(): void {
    spawned.clear();
}

/** Takes a decoration's standing model away, to be carried. */
export function liftOut(house: House, entry: DecorEntry): void {
    const record = spawned.get(house.world);

    record?.objects.get(entry.n)?.destroy();
    record?.objects.delete(entry.n);
}

/** Stands a decoration's model in the home, if the home is furnished now. */
export function standIn(house: House, entry: DecorEntry): void {
    const record = spawned.get(house.world);
    const object = record ? createDecor(entry, house.world, true) : null;

    if (record && object) {
        record.objects.set(entry.n, object);
    }
}
