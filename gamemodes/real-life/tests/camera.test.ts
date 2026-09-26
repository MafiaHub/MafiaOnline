import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { test } from 'node:test';
import { CameraRecorder } from '../src/client/recorder';
import {
    START_CAMERA,
    isCameraPaths,
    type CameraPaths,
    type CameraPose,
    type RecorderState,
} from '../src/shared/camera';
import { loadCameraPaths, saveCameraPaths } from '../src/server/camera-paths';
import loginCamera from '../src/shared/login-camera.json';

test('recorder moves, captures poses, keeps separate splines and saves replayable paths', () => {
    const camera = {
        pose: structuredClone(START_CAMERA),
        editing: false,
        apply(pose: CameraPose) {
            this.pose = pose;
        },
    };
    let state: RecorderState | undefined;
    let saved: CameraPaths | undefined;
    const recorder = new CameraRecorder(
        camera,
        (next) => (state = next),
        (paths) => (saved = paths),
        'freeride',
        () => {},
    );

    recorder.command({ action: 'toggle' });
    assert.equal(recorder.active, false);
    recorder.enabled = true;
    recorder.command({ action: 'toggle' });
    recorder.command({ action: 'point' });
    recorder.command({ action: 'new' });
    assert.equal(state!.status, 'needPoints');
    recorder.command({ action: 'keys', keys: ['KeyW', 'KeyE'] });
    recorder.command({ action: 'roll', x: 75 });
    recorder.update(1000);
    recorder.command({ action: 'keys', keys: [] });
    const second = structuredClone(camera.pose);

    assert.ok(second.position.y > START_CAMERA.position.y);
    assert.ok(Math.abs(second.roll! - 0.3) < 1e-9);
    assert.equal(state!.roll, second.roll);
    recorder.command({ action: 'point' });
    recorder.command({ action: 'play' });
    recorder.command({ action: 'roll', x: -150 });
    recorder.update(20_000);
    assert.ok(Math.abs(camera.pose.roll! - second.roll!) < 1e-9);
    assert.ok(
        Math.hypot(
            camera.pose.position.x - second.position.x,
            camera.pose.position.y - second.position.y,
            camera.pose.position.z - second.position.z,
        ) < 1e-9,
    );

    assert.equal(state!.replaying, false);
    recorder.command({ action: 'new' });
    recorder.command({ action: 'point' });
    recorder.command({ action: 'keys', keys: ['KeyD'] });
    recorder.update(1000);
    recorder.command({ action: 'point' });
    recorder.command({ action: 'save' });
    assert.ok(isCameraPaths(saved));
    assert.equal(saved!.splines.length, 2);
    assert.deepEqual(saved!.splines[0].points[0].position, START_CAMERA.position);
    assert.deepEqual(saved!.splines[0].points[1].position, second.position);
    assert.equal(saved!.splines[0].points[0].roll, 0);
    assert.equal(saved!.splines[0].points[1].roll, second.roll);
    recorder.saved(true);
    assert.equal(state!.dirty, false);
    recorder.command({ action: 'toggle' });
    assert.equal(camera.editing, false);
});

test('camera JSON survives reopening, rejects malformed paths and stays mission-specific', () => {
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const folder = mkdtempSync(resolve(parent, 'camera-'));
    const filename = resolve(folder, 'camera-paths.json');
    const paths: CameraPaths = {
        version: 1,
        mission: 'freeride',
        splines: [
            { seconds: 10, points: [structuredClone(START_CAMERA), structuredClone(START_CAMERA)] },
        ],
    };

    try {
        assert.ok(isCameraPaths(loginCamera));
        assert.deepEqual(loadCameraPaths(filename, loginCamera.mission), loginCamera);
        assert.deepEqual(loadCameraPaths(filename, 'freeridenoc').splines, []);
        saveCameraPaths(filename, paths);
        assert.deepEqual(loadCameraPaths(filename, 'freeride'), paths);
        assert.deepEqual(loadCameraPaths(filename, loginCamera.mission), loginCamera);
        paths.splines[0].points[1].roll = 0.4;
        saveCameraPaths(filename, paths);
        assert.deepEqual(loadCameraPaths(filename, 'freeride'), paths);

        for (const roll of [NaN, Infinity, -Infinity, '0', null]) {
            assert.equal(
                isCameraPaths({
                    ...paths,
                    splines: [{ seconds: 10, points: [START_CAMERA, { ...START_CAMERA, roll }] }],
                }),
                false,
            );
        }

        assert.deepEqual(loadCameraPaths(filename, 'freeridenoc').splines, []);
        assert.equal(
            isCameraPaths({ ...paths, splines: [{ seconds: 0, points: paths.splines[0].points }] }),
            false,
        );

        assert.equal(
            isCameraPaths({ ...paths, splines: [{ seconds: 10, points: [START_CAMERA] }] }),
            false,
        );

        assert.equal(
            isCameraPaths({
                ...paths,
                splines: [
                    {
                        seconds: 10,
                        points: [
                            { position: START_CAMERA.position, direction: { x: 0, y: 0, z: 0 } },
                            START_CAMERA,
                        ],
                    },
                ],
            }),
            false,
        );

        paths.mission = loginCamera.mission;
        saveCameraPaths(filename, paths);
        assert.deepEqual(loadCameraPaths(filename, loginCamera.mission), paths);
    } finally {
        rmSync(folder, { recursive: true });
    }
});
