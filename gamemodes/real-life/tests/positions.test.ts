import assert from 'node:assert/strict';
import { test, type TestContext } from 'node:test';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { build } from 'esbuild';
import { runInNewContext } from 'node:vm';
import { Accounts } from '../src/server/accounts';
import { Positions } from '../src/server/positions';
import type { Session } from '../src/server/session';
import { EVENT } from '../src/shared/protocol';

function setup(t: TestContext) {
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const directory = mkdtempSync(resolve(parent, 'positions-'));
    const accounts = new Accounts(':memory:');
    const account = accounts.create('Recorder', 'hash', 'en')!;

    accounts.setRole(account.id, 'admin');
    const messages: string[] = [];
    const requests: any[] = [];
    let vehicle: { id: number; model: string } | null = null;
    const player = {
        id: 1,
        spawned: true,
        alive: true,
        virtualWorld: 0,
        model: 'Tommy.i3d',
        getVehicle: () => vehicle,
        emit: (event: string, data: string) => {
            assert.equal(event, EVENT.positionCapture);
            requests.push(JSON.parse(data));
        },
        sendMessage: (text: string) => {
            assert.ok(Buffer.byteLength(text) <= 400);
            messages.push(text);
        },
    } as unknown as Player;
    const session = { player, account, entered: true, locale: 'en' } as Session;

    Object.assign(globalThis, {
        World: {
            isReady: () => true,
            getMission: () => 'freeride_extended',
            getMissionGeneration: () => 7,
        },
    });

    const recorder = new Positions(directory, accounts, new Map([[1, session]]));
    let now = 10000;

    t.mock.method(Date, 'now', () => now);
    const record = (comment: string) => {
        now += 1000;
        recorder.handle(player, 'savepos', comment.split(' '));
    };
    const pose = {
        position: { x: 123, y: -4.5, z: 98 },
        rotation: { w: Math.SQRT1_2, x: Math.SQRT1_2, y: 0, z: 0 },
    };
    const response = () => ({
        ...requests.at(-1),
        vehicleId: vehicle?.id ?? null,
        transform: structuredClone(pose),
    });

    t.after(() => {
        accounts.close();
        rmSync(directory, { recursive: true, force: true });
    });

    return {
        recorder,
        account,
        accounts,
        player,
        session,
        messages,
        requests,
        record,
        response,
        pose,
        enterCar: () => {
            vehicle = { id: 9, model: 'taxi00.i3d' };
        },
    };
}

test('savepos appends commented player and passenger vehicle native transforms to JSONL', (t) => {
    const s = setup(t);

    s.record('Front door "north"');
    s.recorder.captured(s.player, s.response());
    s.enterCar();
    s.record('Garage parking space');
    s.recorder.captured(s.player, s.response());
    const records = readFileSync(s.recorder.file, 'utf8')
        .trim()
        .split('\n')
        .map((line) => JSON.parse(line));

    assert.equal(records.length, 2);
    assert.equal(records[0].comment, 'Front door "north"');
    assert.equal(records[0].kind, 'player');
    assert.equal(records[1].kind, 'vehicle');
    assert.equal(records[1].model, 'taxi00.i3d');
    assert.equal(records[1].mission, 'freeride_extended');
    assert.equal(records[1].account, 'Recorder');
    assert.deepEqual(records[1].position, s.pose.position);
    assert.deepEqual(records[1].rotation, s.pose.rotation);
    s.recorder.captured(s.player, s.response());
    assert.equal(readFileSync(s.recorder.file, 'utf8').trim().split('\n').length, 2);
});

test('savepos rejects unauthorized, unsolicited, stale and invalid captures', (t) => {
    const s = setup(t);

    s.accounts.setRole(s.account.id, 'user');
    s.record('Denied');
    assert.equal(s.requests.length, 0);
    s.accounts.setRole(s.account.id, 'admin');
    s.record('Wrong token');
    s.recorder.captured(s.player, { ...s.response(), request: 'forged' });
    assert.equal(existsSync(s.recorder.file), false);
    s.accounts.setRole(s.account.id, 'user');
    s.recorder.captured(s.player, s.response());
    assert.equal(existsSync(s.recorder.file), false);
    s.accounts.setRole(s.account.id, 'admin');
    for (const change of [
        { generation: 6 },
        { world: 2 },
        { vehicleId: 99 },
        { transform: { ...s.pose, rotation: { w: 0, x: 0, y: 0, z: 0 } } },
        { transform: { ...s.pose, position: { x: NaN, y: 0, z: 0 } } },
    ]) {
        s.record('Invalid');
        s.recorder.captured(s.player, { ...s.response(), ...change });
        assert.equal(existsSync(s.recorder.file), false);
    }

    s.record('Car changed during capture');
    const response = s.response();

    s.enterCar();
    s.recorder.captured(s.player, response);
    assert.equal(existsSync(s.recorder.file), false);
});

test('client captures live player and vehicle poses instead of their replicated positions', async () => {
    const bundle = await build({
        entryPoints: ['src/client/positions.ts'],
        bundle: true,
        write: false,
        format: 'iife',
        platform: 'neutral',
    });
    let capture: (value: unknown) => void = () => {};
    const emitted: any[] = [];
    const live = { position: { x: 1, y: 2, z: 3 }, rotation: { w: 1, x: 0, y: 0, z: 0 } };
    const carPose = { ...live, position: { x: 20, y: 30, z: 40 } };
    let vehicle: unknown = null;

    runInNewContext(bundle.outputFiles[0].text, {
        Events: {
            on: (_name: string, callback: typeof capture) => {
                capture = callback;
            },
            emitServer: (_name: string, value: unknown) => emitted.push(value),
        },
        World: { getMissionGeneration: () => 7 },
        LocalPlayer: {
            spawned: true,
            alive: true,
            virtualWorld: 0,
            position: { x: 999, y: 999, z: 999 },
            getVehicle: () => vehicle,
            getWorldTransform: () => live,
        },
    });

    capture({ request: 'foot', generation: 7, world: 0 });
    assert.deepEqual(emitted[0].transform, live);
    assert.equal(emitted[0].vehicleId, null);
    vehicle = { id: 9, getWorldTransform: () => carPose };
    capture({ request: 'car', generation: 7, world: 0 });
    assert.deepEqual(emitted[1].transform, carPose);
    assert.equal(emitted[1].vehicleId, 9);
    capture({ request: 'stale', generation: 6, world: 0 });
    assert.equal(emitted.length, 2);
});
