import assert from 'node:assert/strict';
import { test, type TestContext } from 'node:test';
import { Accounts } from '../src/server/accounts';
import { AdminCommands } from '../src/server/admin';
import type { Session } from '../src/server/session';
import { adminText } from '../src/server/messages';
import type { Point } from '../src/shared/protocol';

class TestPlayer {
    spawned = true;
    alive = true;
    health = 67;
    virtualWorld = 0;
    position = { x: 10, y: 4, z: 20 };
    rotation = { w: 1, x: 0, y: 0, z: 0 };
    camera: TestPlayer | null = null;
    car: TestVehicle | null = null;
    messages: string[] = [];
    kicks: string[] = [];
    weapons: number[][] = [];
    inventory = { selected: 0, weapons: [{ weaponId: 10, loaded: 12, reserve: 29 }] };
    canSpawn = true;
    canEnterCar = true;

    constructor(
        readonly id: number,
        readonly nickname: string,
    ) {}

    sendMessage(text: string) {
        assert.ok(Buffer.byteLength(text) <= 400, 'Native chat rejects messages over 400 bytes');
        this.messages.push(text);

        return true;
    }

    kick(reason: string) {
        this.kicks.push(reason);
    }

    setCameraTarget(target: TestPlayer | null) {
        this.camera = target;

        return true;
    }

    getVehicle() {
        return this.car;
    }

    putInVehicle(car: TestVehicle) {
        if (!this.canEnterCar) {
            return false;
        }

        this.car?.occupants.delete(this);
        this.car = car;
        car.occupants.add(this);

        return true;
    }

    getInventory() {
        return structuredClone(this.inventory);
    }

    setInventory(weapons: typeof this.inventory.weapons, selected: number) {
        this.inventory = { weapons, selected };

        return true;
    }

    giveWeapon(id: number, loaded: number, reserve: number) {
        if (!this.alive) {
            return false;
        }

        this.weapons.push([id, loaded, reserve]);

        return true;
    }

    spawn(position: Point) {
        if (!this.canSpawn) {
            return false;
        }

        this.position = { ...position };
        this.car?.occupants.delete(this);
        this.car = null;
        this.health = 100;
        this.inventory = { selected: 0, weapons: [] };

        return true;
    }

    setHealth(health: number) {
        this.health = health;

        return true;
    }

    setVirtualWorld(world: number) {
        this.virtualWorld = world;

        return true;
    }
}

class TestVehicle {
    radarVisible = false;
    setRadarMarker(visible: boolean) {
        this.radarVisible = visible;

        return true;
    }

    virtualWorld = 0;
    destroyed = false;
    occupants = new Set<TestPlayer>();
    rotation = { w: 1, x: 0, y: 0, z: 0 };

    constructor(
        readonly id: number,
        readonly model: string,
        readonly position: Point,
    ) {}

    setVirtualWorld(world: number) {
        this.virtualWorld = world;
    }

    getOccupants() {
        return [...this.occupants];
    }

    destroy() {
        this.destroyed = true;

        return true;
    }
}

function setup(t: TestContext) {
    const accounts = new Accounts(':memory:');
    const sessions = new Map<number, Session>();
    const saved: number[] = [];
    const players: TestPlayer[] = [];
    const cars: TestVehicle[] = [];
    const weapons = [
        { weaponId: 2, name: 'Bat', kind: 'melee', magazine: 1 },
        { weaponId: 10, name: 'Thompson', kind: 'firearm', magazine: 50 },
        { weaponId: 15, name: 'Grenade', kind: 'throwable', magazine: 1 },
    ];
    const commands = new AdminCommands(accounts, sessions, (session) =>
        saved.push(session.player.id),
    );

    Object.assign(globalThis, {
        World: {
            getPlayers: () => players,
            getVehicles: () => cars.filter((car) => !car.destroyed),
            isReady: () => true,
        },
        Vehicle: {
            spawn: (model: string, position: Point) => {
                const car = new TestVehicle(cars.length + 1, model, position);

                cars.push(car);

                return car;
            },
        },
        Weapon: {
            get: (id: number) => weapons.find((weapon) => weapon.weaponId === id) ?? null,
            list: () => weapons,
        },
    });

    t.after(() => accounts.close());

    function connect(name: string, role: 'admin' | 'user' = 'user') {
        const account = accounts.create(name, 'hash', 'en')!;
        const player = new TestPlayer(players.length + 1, name);
        const session: Session = {
            player: player as unknown as Player,
            account,
            entered: true,
            locale: 'en',
            busy: false,
            respawnAt: 0,
            rememberUntil: 0,
        };

        accounts.setRole(account.id, role);
        players.push(player);
        sessions.set(player.id, session);

        return { player, session, account };
    }

    function run(player: TestPlayer, command: string, ...args: string[]) {
        return commands.handle(player as unknown as Player, command, args);
    }

    return { accounts, commands, sessions, players, cars, saved, connect, run };
}

test('every privileged command checks the stored rank and rejects unauthenticated users', (t) => {
    const { accounts, commands, connect, run, cars } = setup(t);
    const { player, session, account } = connect('Angelo');

    // Even a stale/forged in-memory admin value cannot override the persisted rank.
    session.account!.role = 'admin';

    for (const command of [
        'car',
        'kick',
        'ban',
        'unban',
        'follow',
        'tp',
        'goto',
        'weapon',
        'giveweapon',
        'weapons',
    ]) {
        assert.equal(run(player, command, 'Angelo'), true);
        assert.equal(player.messages.at(-1), adminText('en', 'denied'));
    }

    assert.equal(cars.length, 0);
    assert.equal(accounts.getBan(account.id), undefined);
    assert.equal(run(player, 'setrole', 'Angelo', 'admin'), false);
    assert.equal(accounts.find('Angelo')?.role, 'user');
    commands.handleConsole('setrole', ['Angelo', 'admin']);
    assert.equal(accounts.find('Angelo')?.role, 'admin');
    run(player, 'car');
    assert.equal(cars.length, 1);
    assert.equal(cars[0].radarVisible, true);
    commands.handleConsole('setrole', ['Angelo', 'user']);
    run(player, 'car');
    assert.equal(player.messages.at(-1), adminText('en', 'denied'));
    session.account = undefined;
    run(player, 'car');
    assert.equal(player.messages.at(-1), adminText('en', 'loginRequired'));
});

test('help lists property use and all editor commands according to rank', (t) => {
    const { connect, run } = setup(t);
    const user = connect('Visitor').player;
    const admin = connect('Editor', 'admin').player;

    run(user, 'help');
    assert.match(user.messages.join('\n'), /house_buy/);
    assert.doesNotMatch(user.messages.join('\n'), /house_entry|savepos/);
    run(admin, 'help');
    const help = admin.messages.join('\n');

    for (const command of [
        'house_entry',
        'house_garage',
        'house_set',
        'interior_entry',
        'interior_exit',
        'interior_go',
        'interior_remove',
        'savepos',
    ]) {
        assert.ok(help.includes('/' + command), command);
    }
});

test('follow changes only the camera and resets on off, demotion and resource cleanup', (t) => {
    const { commands, connect, run } = setup(t);
    const { player } = connect('Admin', 'admin');
    const target = connect('Tommy').player;
    const originalPosition = { ...player.position };

    run(player, 'follow', `#${target.id}`);
    assert.equal(player.camera, target);
    assert.deepEqual(player.position, originalPosition);
    run(player, 'follow', 'missing');
    assert.equal(player.camera, target);
    run(player, 'follow', 'off');
    assert.equal(player.camera, null);
    run(player, 'follow', 'tommy');
    commands.handleConsole('setrole', ['Admin', 'user']);
    assert.equal(player.camera, null);
    commands.handleConsole('setrole', ['Admin', 'admin']);
    run(player, 'follow', 'Tommy');
    commands.reset();
    assert.equal(player.camera, null);
});

test('teleport leaves a seat and preserves health, inventory and the selected weapon', (t) => {
    const { connect, run, saved } = setup(t);
    const { player } = connect('Admin', 'admin');
    const target = connect('Tommy').player;
    const inventory = player.getInventory();

    target.position = { x: 100, y: 12, z: -90 };
    target.virtualWorld = 7;
    run(player, 'car');
    const previousCar = player.car!;

    run(player, 'follow', 'Tommy');
    run(player, 'tp', 'Tommy');
    assert.deepEqual(player.position, { x: 102, y: 12.2, z: -90 });
    assert.equal(player.virtualWorld, 7);
    assert.equal(player.health, 67);
    assert.deepEqual(player.getInventory(), inventory);
    assert.equal(player.camera, null);
    assert.equal(player.car, null);
    assert.equal(previousCar.occupants.size, 0);
    assert.deepEqual(saved, [player.id]);
    player.canSpawn = false;
    run(player, 'tp', 'Tommy');
    assert.equal(player.messages.at(-1), adminText('en', 'failed'));
    assert.deepEqual(player.getInventory(), inventory);
    run(player, 'tp', 'Admin');
    assert.equal(player.messages.at(-1), adminText('en', 'selfTarget'));
});

test('cars replace earlier empty admin cars while preserving occupied and unrelated vehicles', (t) => {
    const { commands, cars, connect, run } = setup(t);
    const { player } = connect('Admin', 'admin');
    const passenger = connect('Tommy').player;
    let now = 10_000;

    t.mock.method(Date, 'now', () => now);
    run(player, 'car', '../../unsafe.i3d');
    assert.equal(cars.length, 0);
    run(player, 'car', 'taxi00.i3d');
    assert.deepEqual(cars[0].position, { x: 14, y: 4, z: 20 });
    cars[0].occupants.add(passenger);
    run(player, 'car');
    assert.equal(cars.length, 1);
    now += 1001;
    run(player, 'car');
    commands.update();
    assert.equal(cars.length, 2);
    assert.equal(cars[0].destroyed, false);
    cars[0].occupants.clear();
    commands.update();
    assert.equal(cars[0].destroyed, true);
    now += 1001;
    player.canEnterCar = false;
    run(player, 'car');
    assert.equal(cars[2].destroyed, true);
    assert.equal(cars[1].destroyed, false);
    commands.release(player as unknown as Player);
    cars[1].occupants.clear();
    const unrelated = new TestVehicle(99, 'taxi00.i3d', player.position);

    cars.push(unrelated);
    commands.update();
    assert.equal(cars[1].destroyed, true);
    commands.reset();
    assert.equal(unrelated.destroyed, false);
});

test('weapons support each native kind, target players and reject invalid ammunition', (t) => {
    const { connect, run } = setup(t);
    const { player } = connect('Admin', 'admin');
    const target = connect('Tommy');

    target.session.locale = 'cs';
    run(player, 'weapon', '10');
    run(player, 'weapon', '2');
    run(player, 'giveweapon', '15', '8', `#${target.player.id}`);
    assert.deepEqual(player.weapons, [
        [10, 50, 150],
        [2, 1, 0],
    ]);

    assert.deepEqual(target.player.weapons, [[15, 8, 0]]);
    assert.equal(target.player.messages.at(-1), adminText('cs', 'weaponReceived', { weapon: 15 }));

    for (const ammo of ['0', '-1', '65536', 'NaN', '1.5']) {
        run(player, 'weapon', '10', ammo);
        assert.equal(player.messages.at(-1), adminText('en', 'weaponUsage'));
    }

    run(player, 'weapon', '999');
    assert.equal(player.weapons.length, 2);
    run(player, 'weapons');
    assert.match(player.messages.at(-1)!, /15: Grenade/);
});

test('kick saves first; account bans revoke tokens and remove sessions still waiting to spawn', (t) => {
    const { accounts, commands, connect, run, saved } = setup(t);
    const { player, account } = connect('Admin', 'admin');
    const target = connect('Tommy');
    const token = accounts.remember(target.account);

    target.session.locale = 'cs';
    run(player, 'kick', 'Tommy', 'Test', 'reason');
    assert.deepEqual(saved, [target.player.id]);
    assert.equal(target.player.kicks.at(-1), adminText('cs', 'kicked', { reason: 'Test reason' }));
    assert.equal(accounts.getBan(target.account.id), undefined);
    target.session.entered = false;
    run(player, 'ban', 'Tommy', 'Persistent ban');
    assert.equal(accounts.getBan(target.account.id)?.reason, 'Persistent ban');
    assert.equal(accounts.recall(token.token), undefined);
    assert.equal(
        target.player.kicks.at(-1),
        adminText('cs', 'banned', { reason: 'Persistent ban' }),
    );

    run(player, 'unban', 'Tommy');
    assert.equal(accounts.getBan(target.account.id), undefined);
    assert.equal(accounts.recall(token.token), undefined);
    const offline = accounts.create('Offline', 'hash', 'en')!;

    run(player, 'ban', 'offline');
    assert.ok(accounts.getBan(offline.id));
    commands.handleConsole('unban', ['Offline']);
    assert.equal(accounts.getBan(offline.id), undefined);
    accounts.ban(account.id, 'Banned admin', account.id);
    run(player, 'car');
    assert.equal(player.messages.at(-1), adminText('en', 'denied'));
});
