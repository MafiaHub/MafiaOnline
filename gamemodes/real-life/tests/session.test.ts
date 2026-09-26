import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
import { test } from 'node:test';
import { runInNewContext } from 'node:vm';
import { build } from 'esbuild';
import { Accounts } from '../src/server/accounts';
import { EVENT, isModel, RESOURCE, type Point, type SessionState } from '../src/shared/protocol';

type Handler = (...args: unknown[]) => unknown;

class TestPlayer {
    spawned = false;
    alive = false;
    model = '';
    money = 25;
    setMoney(amount: number) {
        this.money = amount;

        return true;
    }

    nickname = 'Guest';
    virtualWorld = 0;
    position = { x: 0, y: 0, z: 0 };
    rotation = { w: 1, x: 0, y: 0, z: 0 };
    states: SessionState[] = [];
    kicks: string[] = [];
    messages: string[] = [];
    sendMessage(text: string) {
        this.messages.push(text);

        return true;
    }

    constructor(readonly id: number) {}
    getIP() {
        return `127.0.0.${this.id}`;
    }

    emit(_event: string, data: string) {
        if (_event === EVENT.state) {
            this.states.push(JSON.parse(data));
        }
    }

    getSuggestedSpawn() {
        return { x: 10, y: 4, z: 20 };
    }

    getVehicle() {
        return null;
    }

    setVirtualWorld(world: number) {
        this.virtualWorld = world;
    }

    setNickname(name: string) {
        this.nickname = name;
    }

    setModel(model: string) {
        this.model = model;

        return true;
    }

    spawn(position: Point, heading: number) {
        this.position = { ...position };
        this.rotation = { w: Math.cos(heading / 2), x: 0, y: Math.sin(heading / 2), z: 0 };
        this.spawned = true;
        this.alive = true;

        return true;
    }

    despawn() {
        this.spawned = false;
        this.alive = false;
    }

    setCameraTarget() {
        return true;
    }

    kick(reason: string) {
        this.kicks.push(reason);
    }
}

test('resource gates spawning, restores disconnected players and rejects concurrent accounts', async () => {
    const bundle = await build({
        entryPoints: ['src/server/main.ts'],
        bundle: true,
        write: false,
        platform: 'node',
        format: 'cjs',
    });
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const folder = mkdtempSync(resolve(parent, 'session-'));
    const players: TestPlayer[] = [];
    let ready = true;
    let handlers = new Map<string, Handler>();
    let timers: (() => void)[] = [];
    const logs: string[] = [];
    const require = createRequire(import.meta.url);

    function boot() {
        handlers = new Map();
        timers = [];
        runInNewContext(bundle.outputFiles[0].text, {
            require,
            __dirname: resolve(folder, 'resources/lhrp'),
            process: { env: {}, stderr: { write: (text: string) => logs.push(text) } },
            setInterval: (handler: () => void) => {
                timers.push(handler);

                return timers.length;
            },
            clearInterval: () => {},
            Events: {
                on: (name: string, handler: Handler) => handlers.set(name, handler),
                onClient: (name: string, handler: Handler) => handlers.set(name, handler),
            },
            World: {
                getMission: () => 'freeride',
                getMissionGeneration: () => 1,
                isReady: () => ready,
                getPlayers: () => players,
                getVehicles: () => [],
            },
        });

        handlers.get('resourceStart')!(RESOURCE);
    }

    function connect(id: number) {
        const player = new TestPlayer(id);

        players.push(player);
        handlers.get('playerConnect')!(player);

        return player;
    }

    function auth(
        player: TestPlayer,
        mode = 'login',
        username = 'Angelo',
        password = 'long-password',
        remember = false,
    ) {
        return handlers.get(EVENT.auth)!(player, {
            generation: 1,
            mode,
            username,
            password,
            locale: 'cs',
            remember,
        });
    }

    boot();

    try {
        const first = connect(1);

        timers[0]();
        assert.equal(first.spawned, false);
        assert.notEqual(first.virtualWorld, 0);
        await auth(first, 'register', 'Angelo', 'long-password', true);
        const remembered = first.states.at(-1)?.remembered;

        assert.ok(remembered);
        assert.equal(first.spawned, false);
        handlers.get(EVENT.rememberedReady)!(first);
        assert.equal(first.spawned, true);
        assert.equal(first.nickname, 'Angelo');
        assert.ok(isModel(first.model));
        assert.equal(first.virtualWorld, 0);
        assert.deepEqual(first.messages, ['Ahoj, hráč Angelo se připojil!']);
        timers[0]();
        assert.equal(first.messages.length, 1);
        const english = connect(90);

        await handlers.get(EVENT.auth)!(english, {
            generation: 1,
            mode: 'register',
            username: 'English',
            password: 'long-password',
            locale: 'en',
        });

        assert.equal(english.messages.at(-1), 'Hey, English connected!');
        assert.equal(first.messages.at(-1), 'Ahoj, hráč English se připojil!');
        handlers.get('playerDisconnect')!(english);
        players.splice(players.indexOf(english), 1);
        const second = connect(2);

        const resume = (player: TestPlayer) =>
            handlers.get(EVENT.auth)!(player, {
                generation: 1,
                mode: 'remembered',
                username: 'Angelo',
                token: remembered.token,
                locale: 'cs',
            });

        await resume(second);
        assert.equal(second.states.at(-1)?.error, 'alreadyOnline');
        assert.equal(second.spawned, false);

        const expected = { x: -700, y: 14, z: 800 };

        first.position = expected;
        first.rotation = { w: Math.cos(0.6), x: 0, y: Math.sin(0.6), z: 0 };
        handlers.get('playerDisconnect')!(first);
        players.splice(players.indexOf(first), 1);
        const savedModel = first.model;

        await auth(second, 'login', 'Angelo', 'incorrect-password');
        assert.equal(second.states.at(-1)?.error, 'invalidCredentials');
        assert.equal(second.spawned, false);

        ready = false;
        await resume(second);
        assert.equal(second.states.at(-1)?.phase, 'spawning');
        assert.equal(second.spawned, false);
        ready = true;
        timers[0]();
        assert.deepEqual(second.position, expected);
        assert.equal(second.model, savedModel);
        assert.ok(Math.abs(second.rotation.y - Math.sin(0.6)) < 1e-10);

        // Late crypto completion cannot create/authenticate an abandoned session.
        const abandoned = connect(3);
        const pending = auth(abandoned, 'register', 'Abandoned');

        handlers.get('playerDisconnect')!(abandoned);
        players.splice(players.indexOf(abandoned), 1);
        await pending;
        assert.equal(abandoned.spawned, false);

        handlers.get('resourceStop')!(RESOURCE);
        boot();
        assert.equal(second.spawned, false);
        await resume(second);
        assert.deepEqual(second.position, expected);
        assert.equal(second.model, savedModel);
        handlers.get('playerCommand')!(second, 'logout', []);
        assert.equal(second.spawned, false);
        assert.equal(second.states.at(-1)?.forgetRemembered, true);
        await resume(second);
        assert.equal(second.states.at(-1)?.error, 'rememberedExpired');
        assert.equal(second.spawned, false);
        await auth(second);
        assert.equal(second.spawned, true);
        assert.equal(second.states.at(-1)?.remembered, undefined);

        const accounts = new Accounts(resolve(folder, 'data/accounts.sqlite'));
        const account = accounts.find('Angelo')!;

        accounts.ban(account.id, 'Test ban', account.id);
        const banned = connect(4);

        await auth(banned);
        assert.equal(banned.states.at(-1)?.error, 'banned');
        assert.equal(banned.spawned, false);
        accounts.unban(account.id);
        handlers.get('playerDisconnect')!(second);
        players.splice(players.indexOf(second), 1);
        ready = false;
        await auth(banned);
        assert.equal(banned.states.at(-1)?.phase, 'spawning');
        accounts.ban(account.id, 'Banned while loading', account.id);
        ready = true;
        timers[0]();
        assert.equal(banned.spawned, false);
        assert.equal(banned.kicks.length, 1);
        accounts.close();
        assert.deepEqual(logs, []);
    } finally {
        handlers.get('resourceStop')!(RESOURCE);
        rmSync(folder, { recursive: true });
    }
});
