// The burning building's rooms (src/server/firefighters/plans.ts): every fire
// stands inside its room, nobody lies in the flames, and the crew arrives
// clear of the fires and beyond the reach of the door they came through.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import * as rooms from '../src/server/lib/house_rooms';
import { INSIDE_DOOR_REACH, PLANS, inward } from '../src/server/firefighters/plans';

// How far outside a fire's radius a person lies, and the crew arrives.
const PERSON_CLEARANCE = 0.6;
const ARRIVAL_CLEARANCE = 0.4;

type Flat = { x: number; z: number };

const gap = (a: Flat, b: Flat): number => Math.hypot(a.x - b.x, a.z - b.z);
const keys = Object.keys(PLANS) as rooms.RoomKey[];

// Where a room's arrivals are, room-local: from the street and back from each other room.
function arrivals(key: rooms.RoomKey): Flat[] {
    const room = rooms.ROOMS[key];

    return [room.arrive, ...Object.values(room.returns ?? {})].map((pose) => {
        const moved = inward(pose);

        return { x: moved.position.x, z: moved.position.z };
    });
}

test('every room of every home kind has a plan', () => {
    for (const kind of Object.values(rooms.KINDS)) {
        for (const key of kind.rooms) {
            assert.ok(PLANS[key], `${key} has no plan`);
        }
    }
});

test('fires stand inside their room, and no more are lit than it has', () => {
    for (const key of keys) {
        const { bounds } = rooms.ROOMS[key];
        const plan = PLANS[key];

        assert.ok(plan.lit >= 1 && plan.lit <= plan.fires.length, `${key} lights ${plan.lit}`);

        for (const fire of plan.fires) {
            assert.ok(
                fire.x >= bounds.x0 &&
                    fire.x <= bounds.x1 &&
                    fire.z >= bounds.z0 &&
                    fire.z <= bounds.z1,
                `${key}: ${fire.what} at ${fire.x}, ${fire.z} is outside the room`,
            );
        }
    }
});

test('people lie clear of every fire in their room', () => {
    for (const key of keys) {
        for (const person of PLANS[key].people) {
            for (const fire of PLANS[key].fires) {
                const clearance = gap(person, fire) - fire.radius;

                assert.ok(
                    clearance >= PERSON_CLEARANCE,
                    `${key}: someone at ${person.x}, ${person.z} is ${clearance.toFixed(2)} m from ${fire.what}`,
                );
            }
        }
    }
});

test('the crew arrives clear of the fires and out of reach of the doors', () => {
    for (const key of keys) {
        const doors = rooms.ROOMS[key].doors.map((door) => door.at);

        for (const arrival of arrivals(key)) {
            for (const fire of PLANS[key].fires) {
                const clearance = gap(arrival, fire) - fire.radius;

                assert.ok(
                    clearance >= ARRIVAL_CLEARANCE,
                    `${key}: arriving at ${arrival.x.toFixed(2)}, ${arrival.z.toFixed(2)} is ${clearance.toFixed(2)} m from ${fire.what}`,
                );
            }

            for (const door of doors) {
                assert.ok(
                    gap(arrival, door) > INSIDE_DOOR_REACH,
                    `${key}: arriving within reach of the door at ${door.x}, ${door.z}`,
                );
            }
        }
    }
});
