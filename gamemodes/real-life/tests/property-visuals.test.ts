import assert from 'node:assert/strict';
import { test } from 'node:test';
import { build } from 'esbuild';
import { runInNewContext } from 'node:vm';
import { housePose, arrowRotation } from '../src/client/property-visuals';
import { PROPERTY_EVENT } from '../src/shared/properties';
import type { Point } from '../src/shared/protocol';

test('stock arrow always points down while its face turns toward the viewer', () => {
    for (const viewer of [
        { x: 5, y: 3, z: 4 },
        { x: -3, y: 1, z: 0 },
        { x: 0, y: 0, z: -4 },
    ]) {
        const q = arrowRotation({ x: 0, y: 0, z: 0 }, viewer);

        assert.ok(Math.abs(1 - 2 * (q.y * q.y + q.z * q.z)) < 1e-9);
        // LS3D S_quat::RotationMatrix, row vector (1,0,0) * matrix.
        assert.ok(Math.abs(2 * (q.x * q.y - q.w * q.z) + 1) < 1e-9);
        assert.ok(Math.abs(2 * (q.x * q.z + q.w * q.y)) < 1e-9);
    }

    assert.equal(housePose({ x: 4, y: 2, z: 8 }, 10).position.y, 2.65);
});

test('production markers stay at entrances when the player moves and use native position for interaction', async () => {
    const bundle = await build({
        entryPoints: ['src/client/properties.ts'],
        bundle: true,
        write: false,
        format: 'iife',
        platform: 'neutral',
    });
    const handlers = new Map<string, (...args: any[]) => void>();
    const emitted: { event: string; payload: unknown }[] = [];
    const frames: { position?: Point; destroyed: boolean; model: string }[] = [];
    let native = { x: 10, y: 2, z: 10 };
    let enter: () => void = () => {};

    runInNewContext(bundle.outputFiles[0].text, {
        Events: {
            on: (name: string, callback: (...args: any[]) => void) => handlers.set(name, callback),
            emitServer: (event: string, payload: unknown) => emitted.push({ event, payload }),
        },
        LocalPlayer: {
            spawned: true,
            alive: true,
            virtualWorld: 0,
            position: { x: 1000, y: 2, z: 1000 },
            getWorldPosition: () => native,
        },
        World: { getMissionGeneration: () => 1 },
        Scene: {
            createModelFrame: (model: string) => {
                const frame = {
                    model,
                    destroyed: false,
                    position: undefined as Point | undefined,
                    id: frames.length,
                    setVisible: () => {},
                    setScale: () => {},
                    setRotation: () => {},
                    setWorldPosition: (p: Point) => {
                        frame.position = { ...p };
                    },
                    destroy: () => {
                        frame.destroyed = true;
                    },
                };

                frames.push(frame);

                return frame;
            },
            playModelAnimation: () => true,
        },
        Key: {
            bind: (_key: string, _state: string, callback: () => void) => {
                enter = callback;
            },
        },
        Draw: { worldText: () => {} },
        Chat: { isOpen: () => false },
        Date: { now: () => 2000 },
    });

    handlers.get(PROPERTY_EVENT.markers)!({
        generation: 1,
        world: 0,
        markers: [
            {
                key: 'house:1:0',
                position: { x: 10, y: 2, z: 10 },
                kind: 'sale',
                label: 'House #1 · $4778',
            },
            { key: 'public:1', position: { x: 20, y: 2, z: 10 }, kind: 'entry', label: 'Shop' },
        ],
    });

    handlers.get('render')!();
    const positions = structuredClone(frames.map((f) => f.position));

    assert.equal(frames[0].model, '9dumch4.i3d');
    enter();

    assert.equal(emitted.length, 1);
    native = { x: 40, y: 2, z: 50 };
    handlers.get('render')!();
    assert.deepEqual(
        frames.map((f) => f.position),
        positions,
    );

    enter();

    assert.equal(emitted.length, 1);
    handlers.get('missionUnload')!();
    assert.ok(frames.every((f) => f.destroyed));
});
