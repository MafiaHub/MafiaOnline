// The hospital's rooms (src/server/lib/hospital_room.ts): everyone stands in
// a room clear of its walls and furniture, sits on a chair nobody collides
// with, lies on a bed, and the player arrives in the hall with room to move.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import * as hospital from '../src/server/lib/hospital_room';

type Flat = { x0: number; x1: number; z0: number; z1: number };

const WALL_CLEARANCE = 0.3;
const FURNITURE_CLEARANCE = 0.2;

// The footprint of each piece put() placed, from its measured box, turned by its yaw.
function footprints(passable: boolean): Flat[] {
    const boxes: Readonly<Record<string, readonly number[]>> = hospital.BOXES;

    return hospital
        .pieces()
        .filter((piece) => piece.rotation.x === 0 && Boolean(piece.passable) === passable)
        .flatMap((piece) => {
            const box = boxes[piece.model.replace(/\.i3d$/, '')];

            if (!box) {
                return [];
            }

            const [x0, x1, , , z0, z1] = box;
            const scale = piece.scale ?? { x: 1, y: 1, z: 1 };
            const yaw = piece.rotation.y;
            const corners = [
                [x0, z0],
                [x0, z1],
                [x1, z0],
                [x1, z1],
            ].map(([x, z]) => ({
                x: piece.position.x + x * scale.x * Math.cos(yaw) + z * scale.z * Math.sin(yaw),
                z: piece.position.z - x * scale.x * Math.sin(yaw) + z * scale.z * Math.cos(yaw),
            }));

            return [
                {
                    x0: Math.min(...corners.map((c) => c.x)),
                    x1: Math.max(...corners.map((c) => c.x)),
                    z0: Math.min(...corners.map((c) => c.z)),
                    z1: Math.max(...corners.map((c) => c.z)),
                },
            ];
        });
}

function within(box: Flat, x: number, z: number, margin = 0): boolean {
    return (
        x >= box.x0 - margin && x <= box.x1 + margin && z >= box.z0 - margin && z <= box.z1 + margin
    );
}

const spots = Object.entries(hospital.SPOTS).flatMap(([name, value]) => {
    const list: readonly hospital.Spot[] = 'posture' in value ? [value] : value;

    return list.map((spot) => ({ name, spot }));
});

test('everyone is in a room, clear of its walls', () => {
    for (const { name, spot } of spots) {
        const { x, z } = spot.position;

        assert.ok(
            hospital.roomAt(spot.position, -WALL_CLEARANCE),
            `${name} at ${x}, ${z} is in a wall`,
        );
    }
});

test('standing people and the arrival are clear of the furniture', () => {
    const solid = footprints(false);
    const standing = [
        ...spots.filter(({ spot }) => spot.posture === 'stand'),
        { name: 'arrival', spot: hospital.ARRIVAL },
    ];

    for (const { name, spot } of standing) {
        const { x, z } = spot.position;

        assert.ok(
            !solid.some((box) => within(box, x, z, FURNITURE_CLEARANCE)),
            `${name} at ${x}, ${z} stands in furniture`,
        );
    }
});

test('the seated sit on chairs nobody collides with', () => {
    const passable = footprints(true);
    const solid = footprints(false);

    for (const { name, spot } of spots.filter(({ spot }) => spot.posture === 'sit')) {
        const { x, z } = spot.position;

        assert.ok(
            passable.some((box) => within(box, x, z)),
            `${name} has no passable chair`,
        );

        assert.ok(!solid.some((box) => within(box, x, z)), `${name} sits in something solid`);
    }
});

test('the patients lie on beds', () => {
    const solid = footprints(false);

    for (const { name, spot } of spots.filter(({ spot }) => spot.posture === 'lie')) {
        const { x, z } = spot.position;

        assert.ok(spot.position.y > 0.6, `${name} lies below the mattress`);
        assert.ok(
            solid.some((box) => within(box, x, z, -0.3)),
            `${name} at ${x}, ${z} is off a bed`,
        );
    }
});

test('the reception counter can be reached from the hall in front of the desk', () => {
    const solid = footprints(false);
    const { x, z } = hospital.SPOTS.receptionist.position;
    // Where a customer stands: across the desk, 2 m west of her.
    const customer = { x: x - 2.0, z };

    assert.equal(hospital.roomAt({ ...customer, y: 0 }), 'hall');
    assert.ok(!solid.some((box) => within(box, customer.x, customer.z, FURNITURE_CLEARANCE)));
});
