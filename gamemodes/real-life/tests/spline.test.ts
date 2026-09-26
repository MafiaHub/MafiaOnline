import assert from 'node:assert/strict';
import { test } from 'node:test';
import { catmullRom } from '../src/client/spline';

import { createCameraFlight } from '../src/client/camera-flight';

const points = [
    { x: -160, y: 95, z: -160 },
    { x: 140, y: 115, z: -180 },
    { x: 180, y: 90, z: 150 },
    { x: -150, y: 105, z: 170 },
];

test('Catmull–Rom passes through its inner control points', () => {
    assert.deepEqual(catmullRom(points[0], points[1], points[2], points[3], 0), points[1]);
    assert.deepEqual(catmullRom(points[0], points[1], points[2], points[3], 1), points[2]);
});

test('recorded flight keeps its endpoints and maintains an even speed', () => {
    const sample = createCameraFlight(
        points.map((position) => ({ position, direction: { x: 0, y: 0, z: 1 } })),
    );

    assert.deepEqual(sample(0).position, points[0]);
    assert.deepEqual(sample(1).position, points.at(-1));
    const distances: number[] = [];
    let previous = sample(0).position;

    for (let i = 1; i <= 500; i++) {
        const current = sample(i / 500).position;

        assert.ok(Object.values(current).every(Number.isFinite));
        distances.push(
            Math.hypot(current.x - previous.x, current.y - previous.y, current.z - previous.z),
        );

        previous = current;
    }

    assert.ok(Math.max(...distances) / Math.min(...distances) < 1.02);
});

test('headings cross north without a full turn and stationary shots can pan', () => {
    const position = { x: 1, y: 3, z: 2 };
    const sample = createCameraFlight([
        { position, direction: { x: 0.01, y: 0, z: -1 } },
        { position, direction: { x: -0.01, y: 0, z: -1 } },
    ]);

    assert.deepEqual(sample(0.5).position, position);
    assert.ok(sample(0.5).direction.z < -0.999);
    assert.ok(Math.abs(sample(0.5).direction.x) < 0.001);
});

test('roll interpolates smoothly across 180 degrees and old recordings stay level', () => {
    const pose = { position: points[0], direction: { x: 0, y: 0, z: 1 } };
    const sample = createCameraFlight([
        { ...pose, roll: (170 * Math.PI) / 180 },
        { ...pose, roll: (-170 * Math.PI) / 180 },
    ]);

    assert.ok(Math.abs(sample(0.5).roll! - Math.PI) < 1e-9);
    assert.ok(Math.abs(sample(1).roll! - (190 * Math.PI) / 180) < 1e-9);
    assert.deepEqual(sample(0.5).direction, pose.direction);
    assert.equal(createCameraFlight([pose, pose])(0.5).roll, 0);
});
