// What burns where in a burning building (building.ts): the fires and the
// people of each furnished room, and where the crew arrives in it. Kept apart
// so tests/building_plans.test.ts can check the layout without a server.
import type * as rooms from '../lib/house_rooms';
import type { Placement } from '../lib/util';
import type { FireSpot } from './model';

// A home's arrival point is half a metre from its door, where G would take a
// firefighter straight back out instead of working the extinguisher: the
// crew arrives a metre further in, and a door answers only close to it.
export const INSIDE_DOOR_REACH = 1.1;
export const ARRIVE_INSIDE = 1.0;

/** Where fires and people go in each room, room-local, at its floor. */
export interface RoomPlan {
    /** "the dining room": what a toast calls it. */
    label: string;
    /** The furniture that can burn (lib/house_rooms.ts places it). */
    fires: readonly Omit<FireSpot, 'room'>[];
    /** Open floor where someone can lie, clear of the furniture and the fires. */
    people: readonly { x: number; z: number }[];
    /** How many of its fires burn when the call comes in. */
    lit: number;
}

// Fires stand on the furniture each room is dressed with (house_rooms.ts:
// the piece's floor point, its size from BOXES), so the flames come out of
// the bed or the sofa rather than the bare floor. People lie at least 0.6 m
// outside a fire's radius and clear of every piece. The salon's own sofa and
// armchairs are part of myhouse.4ds, whose positions are not measured, so
// nobody lies there; the villa's residents are in the dining room and the
// bedroom. Nothing burns near the dining room's hearth (x -0.65, z 0.6),
// which a villa's owner has alight in their own world.
export const PLANS: Readonly<Record<rooms.RoomKey, RoomPlan>> = {
    room: {
        label: 'the room',
        fires: [
            { x: 2.3, z: 0.95, radius: 1.0, height: 1.2, what: 'the bed' },
            { x: -1.5, z: 1.4, radius: 0.8, height: 1.0, what: 'the table' },
            { x: 1.6, z: -1.8, radius: 0.7, height: 1.6, what: 'the wardrobe' },
        ],
        people: [
            { x: 0.6, z: 0.6 },
            { x: -2.1, z: -0.3 },
        ],
        lit: 2,
    },
    flat: {
        label: 'the flat',
        fires: [
            { x: 3.6, z: -0.35, radius: 1.2, height: 1.3, what: 'the sofa' },
            { x: 4.4, z: 2.6, radius: 1.2, height: 1.3, what: 'the bed' },
            { x: -0.3, z: -2.0, radius: 0.8, height: 1.6, what: 'the kitchen stove' },
            { x: 0.7, z: 0.9, radius: 0.9, height: 1.0, what: 'the table' },
            { x: 0.0, z: 3.4, radius: 0.7, height: 1.8, what: 'the wardrobe' },
        ],
        people: [
            { x: 2.4, z: 1.4 },
            { x: 5.3, z: 0.8 },
            { x: 1.9, z: -1.6 },
        ],
        lit: 3,
    },
    salon: {
        label: 'the salon',
        fires: [
            { x: 3.0, z: -2.6, radius: 0.7, height: 1.8, what: 'the standard lamp' },
            { x: 6.5, z: -1.5, radius: 0.9, height: 2.6, what: 'the curtains' },
            { x: 6.5, z: 2.2, radius: 0.9, height: 2.6, what: 'the curtains' },
            { x: 3.25, z: 3.9, radius: 0.7, height: 2.0, what: 'the clock' },
            { x: 4.0, z: -0.3, radius: 1.3, height: 1.0, what: 'the rug' },
        ],
        people: [],
        lit: 2,
    },
    dining: {
        label: 'the dining room',
        fires: [
            { x: 3.1, z: 0.6, radius: 1.3, height: 1.2, what: 'the dining table' },
            { x: 1.0, z: 3.4, radius: 0.8, height: 1.8, what: 'the cabinet' },
            { x: 5.0, z: -3.1, radius: 0.8, height: 1.6, what: 'the palm' },
        ],
        people: [
            { x: 5.5, z: 1.9 },
            { x: 2.2, z: -2.6 },
        ],
        lit: 1,
    },
    bedroom: {
        label: 'the bedroom',
        fires: [
            { x: 3.5, z: 2.1, radius: 1.3, height: 1.2, what: 'the bed' },
            { x: 3.6, z: -3.4, radius: 0.9, height: 1.0, what: 'the ottoman' },
            { x: -0.64, z: 1.8, radius: 0.7, height: 2.0, what: 'the wardrobe' },
            { x: 5.44, z: -1.8, radius: 0.8, height: 1.2, what: 'the armchair' },
        ],
        people: [
            { x: 1.6, z: 0.2 },
            { x: 5.3, z: 0.6 },
        ],
        lit: 1,
    },
};

// A room's arrival point, ARRIVE_INSIDE further into the room.
export function inward({ position, heading }: Placement): Placement {
    return {
        position: {
            x: position.x + Math.sin(heading) * ARRIVE_INSIDE,
            y: position.y,
            z: position.z + Math.cos(heading) * ARRIVE_INSIDE,
        },
        heading,
    };
}
