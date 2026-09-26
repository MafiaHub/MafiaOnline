import assert from 'node:assert/strict';
import { test, type TestContext } from 'node:test';
import { mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { Accounts } from '../src/server/accounts';
import { Properties } from '../src/server/properties';
import { PropertyDefinitions } from '../src/server/property-definitions';
import type { Session } from '../src/server/session';
import type { Point } from '../src/shared/protocol';
import { HOUSE_WORLD, INTERIOR_WORLD } from '../src/shared/properties';

class Person {
    spawned = true;
    alive = true;
    money = 25;
    health = 61;
    virtualWorld = 0;
    position = { x: 10, y: 2, z: 10 };
    rotation = { w: 1, x: 0, y: 0, z: 0 };
    car: Car | null = null;
    messages: string[] = [];
    payloads: unknown[] = [];
    inventory = { weapons: [{ weaponId: 10, loaded: 6, reserve: 12 }], selected: 10 };
    constructor(readonly id: number) {}
    sendMessage(text: string) {
        assert.ok(Buffer.byteLength(text) <= 400);
        this.messages.push(text);

        return true;
    }

    emit(_event: string, data: string) {
        this.payloads.push(JSON.parse(data));
    }

    setMoney(amount: number) {
        this.money = amount;

        return true;
    }

    setVirtualWorld(world: number) {
        this.virtualWorld = world;
    }

    getVehicle() {
        return this.car;
    }

    getInventory() {
        return structuredClone(this.inventory);
    }

    setInventory(weapons: typeof this.inventory.weapons, selected: number) {
        this.inventory = { weapons, selected };

        return true;
    }

    setHealth(health: number) {
        this.health = health;

        return true;
    }

    setCameraTarget() {
        return true;
    }

    getSuggestedSpawn() {
        return { x: 0, y: 2, z: 0 };
    }

    spawn(position: Point) {
        this.position = { ...position };
        this.health = 100;
        this.inventory = { weapons: [], selected: 0 };

        return true;
    }
}
class Car {
    virtualWorld = 0;
    terminalState = 0;
    destroyed = false;
    velocity = { x: 0, y: 0, z: 0 };
    rotation = { w: 0.7, x: 0.1, y: 0.7, z: 0.1 };
    occupants: Person[] = [];
    condition = '{"fuel":12.75,"brokenWheel":true}';
    restored = '';
    constructor(
        readonly id: number,
        readonly model: string,
        public position: Point,
    ) {}

    getDriver() {
        return this.occupants[0] ?? null;
    }

    getOccupants() {
        return this.occupants;
    }

    saveState() {
        return this.condition;
    }

    restoreState(text: string) {
        this.restored = text;

        return true;
    }

    setVirtualWorld(world: number) {
        this.virtualWorld = world;
    }

    destroy() {
        this.destroyed = true;

        return true;
    }
}
function setup(t: TestContext) {
    const root = resolve('../../build/real-life-tests');

    mkdirSync(root, { recursive: true });
    const dir = mkdtempSync(resolve(root, 'properties-'));
    const accounts = new Accounts(resolve(dir, 'accounts.sqlite'));
    const sessions = new Map<number, Session>();
    const people: Person[] = [];
    const cars: Car[] = [];
    const claims: number[] = [];

    for (let id = 1; id <= 3; id++) {
        const player = new Person(id);

        people.push(player);
        const account = accounts.create(`Person${id}`, 'hash', id === 3 ? 'cs' : 'en')!;

        if (id === 1) {
            accounts.setRole(account.id, 'admin');
        }

        accounts.setBalance(account.id, 20000);
        player.money = 20000;
        sessions.set(id, {
            player: player as unknown as Player,
            account,
            locale: id === 3 ? 'cs' : 'en',
            entered: true,
            busy: false,
            respawnAt: 0,
            rememberUntil: 0,
        });
    }

    Object.assign(globalThis, {
        World: {
            getMission: () => 'freeride_extended',
            getMissionGeneration: () => 7,
            isReady: () => true,
            getVehicles: () => cars.filter((c) => !c.destroyed),
        },
        Quaternion: class {
            constructor(
                public w: number,
                public x: number,
                public y: number,
                public z: number,
            ) {}
        },
        Vehicle: {
            spawn: (model: string, position: Point) => {
                const car = new Car(cars.length + 10, model, { ...position });

                cars.push(car);

                return car;
            },
        },
    });

    const properties = new Properties(dir, accounts, sessions, (v) => claims.push(v.id));
    const command = (id: number, name: string, ...args: string[]) =>
        properties.handle(people[id - 1] as unknown as Player, name, args);
    const use = (id: number, key: string, world = people[id - 1].virtualWorld, generation = 7) =>
        properties.use(people[id - 1] as unknown as Player, { key, world, generation });

    t.after(() => {
        accounts.close();
        rmSync(dir, { recursive: true, force: true });
    });

    return { dir, accounts, sessions, people, cars, claims, properties, command, use };
}

function record(s: ReturnType<typeof setup>) {
    s.command(1, 'house_entry');
    s.people[0].position = { x: 500, y: 10, z: 500 };
    s.command(1, 'interior_exit', 'flat');
    s.command(1, 'house_set', '1', '4778', 'flat');
    s.people[0].position = { x: 10, y: 2, z: 10 };
}

test('house recording is admin-only; entries and full parking rotation survive reopening', (t) => {
    const s = setup(t);

    s.command(2, 'house_entry');
    assert.equal(s.properties.definitions.houses.length, 0);
    s.command(1, 'house_entry');
    s.people[0].position.x = 20;
    s.command(1, 'house_entry', '1');
    const car = new Car(99, 'taxi00.i3d', { x: 20, y: 2, z: 18 });

    car.occupants = [s.people[0]];
    s.people[0].car = car;
    s.command(1, 'house_garage');
    const definitions = new PropertyDefinitions(s.dir);

    assert.equal(definitions.houses[0].entries.length, 2);
    assert.deepEqual(definitions.houses[0].garages[0], {
        position: car.position,
        rotation: car.rotation,
    });

    assert.equal(readFileSync(resolve(s.dir, 'houses.jsonl'), 'utf8').trim().split('\n').length, 1);
    s.accounts.setRole(s.sessions.get(1)!.account!.id, 'user');
    s.people[0].car = null;
    s.command(1, 'house_entry');
    assert.equal(s.properties.definitions.houses.length, 1);
});

test('purchases are durable and atomic, reject distant buyers and allow multiple homes', (t) => {
    const s = setup(t);

    record(s);
    s.people[1].position.x = 200;
    s.command(2, 'house_buy', '1');
    assert.equal(s.accounts.houseOwner(1), undefined);
    s.people[1].position.x = 10;
    s.command(2, 'house_buy', '1');
    assert.equal(s.accounts.balance(s.sessions.get(2)!.account!.id), 15222);
    s.command(3, 'house_buy', '1');
    assert.equal(s.accounts.balance(s.sessions.get(3)!.account!.id), 20000);
    s.command(1, 'house_entry');
    s.command(1, 'house_set', '2', '4778', 'flat');
    s.command(2, 'house_buy', '2');
    assert.equal(s.accounts.balance(s.sessions.get(2)!.account!.id), 10444);
    const reopened = new Accounts(resolve(s.dir, 'accounts.sqlite'));

    assert.equal(reopened.houseOwner(2)?.accountId, s.sessions.get(2)!.account!.id);
    reopened.close();
    s.command(1, 'house_entry');
    s.command(1, 'house_set', '3', '999999', 'flat');
    s.command(2, 'house_buy', '3');
    assert.equal(s.accounts.houseOwner(3), undefined);
    assert.equal(s.accounts.balance(s.sessions.get(2)!.account!.id), 10444);
});

test('house worlds isolate reused interiors; locks, exits, health and weapons are preserved', (t) => {
    const s = setup(t);
    let now = 10000;

    t.mock.method(Date, 'now', () => now);

    record(s);
    s.use(2, 'house:1:0');
    assert.equal(s.people[1].virtualWorld, 0);
    assert.equal(s.accounts.houseOwner(1), undefined);
    now += 1000;
    s.command(2, 'house_buy', '1');
    s.use(3, 'house:1:0');
    assert.equal(s.people[2].virtualWorld, 0);
    s.command(2, 'house_unlock', '1');
    s.use(2, 'house:1:0');
    assert.equal(s.people[1].virtualWorld, HOUSE_WORLD + 1);
    assert.equal(s.people[1].health, 61);
    assert.equal(s.people[1].inventory.selected, 10);
    now += 1000;
    s.use(3, 'house:1:0');
    assert.equal(s.people[2].virtualWorld, s.people[1].virtualWorld);
    assert.deepEqual(s.properties.savedPlace(s.sessions.get(2)!), {
        mission: 'freeride_extended',
        position: { x: 10, y: 2, z: 10 },
        heading: 0,
    });

    s.command(1, 'house_entry');
    s.command(1, 'house_set', '2', '4778', 'flat');
    s.command(1, 'house_buy', '2');
    s.use(1, 'house:2:0');
    assert.equal(s.people[0].virtualWorld, HOUSE_WORLD + 2);
    assert.deepEqual(s.people[0].position, s.people[1].position);
    s.properties.update();
    assert.match(JSON.stringify(s.people[1].payloads.at(-1)), /"key":"exit"/);
    s.command(2, 'leave');
    assert.equal(s.people[1].virtualWorld, 0);
    assert.deepEqual(s.people[1].position, { x: 10, y: 2, z: 10 });
});

test('public doors share a template world and forged or stale interactions cannot teleport', (t) => {
    const s = setup(t);

    record(s);
    s.command(1, 'interior_entry', 'flat');
    s.use(2, 'public:1', 0, 6);
    assert.equal(s.people[1].virtualWorld, 0);
    s.use(2, 'public:1', HOUSE_WORLD + 1);
    assert.equal(s.people[1].virtualWorld, 0);
    s.use(2, 'public:1');
    assert.equal(s.people[1].virtualWorld, INTERIOR_WORLD + 1);
    s.use(3, 'public:1');
    assert.equal(s.people[2].virtualWorld, s.people[1].virtualWorld);
    s.command(1, 'interior_remove', '1');
    s.command(2, 'leave');
    assert.equal(s.people[1].virtualWorld, 0);
    s.accounts.ban(s.sessions.get(3)!.account!.id, 'test', s.sessions.get(1)!.account!.id);
    s.command(3, 'house_entry');
    assert.equal(s.properties.definitions.houses.length, 1);
});

test('garages keep saved condition across restarts, avoid blocked slots and reject theft and duplicate assignment', (t) => {
    const s = setup(t);

    record(s);
    s.command(2, 'house_buy', '1');
    const car = new Car(99, 'taxi00.i3d', { x: 20, y: 2, z: 20 });

    s.cars.push(car);
    s.people[0].car = car;
    car.occupants = [s.people[0]];
    s.command(1, 'house_garage');
    s.command(1, 'house_garage');
    s.people[0].car = null;
    s.people[2].car = car;
    car.occupants = [s.people[2]];
    s.command(3, 'garage_save', '1', '1');
    assert.equal(s.accounts.garages().length, 0);
    s.people[2].car = null;
    s.people[1].car = car;
    car.occupants = [s.people[1]];
    s.command(2, 'garage_save', '1', '1');
    assert.deepEqual(s.claims, [99]);
    s.command(2, 'garage_save', '1', '2');
    assert.equal(s.accounts.garages().length, 1);
    assert.equal(s.accounts.garages()[0].snapshot, car.condition);
    s.people[1].car = null;
    car.occupants = [];
    s.properties.reset();
    const restarted = new Properties(s.dir, s.accounts, s.sessions, () => {});
    const blocker = new Car(100, 'taxi00.i3d', { ...car.position });

    s.cars.push(blocker);
    restarted.update();
    assert.equal(s.cars.length, 2);
    blocker.destroy();
    restarted.update();
    assert.equal(s.cars.length, 3);
    assert.equal(s.cars[2].restored, car.condition);
    restarted.update();
    assert.equal(s.cars.length, 3);
});

test('lost garage cars wait for occupants and the recovery delay, then restore the last save', (t) => {
    const s = setup(t);
    let now = 10000;

    t.mock.method(Date, 'now', () => now);
    record(s);
    s.command(2, 'house_buy', '1');
    const car = new Car(99, 'taxi00.i3d', { x: 20, y: 2, z: 20 });

    s.cars.push(car);
    s.people[0].car = car;
    car.occupants = [s.people[0]];
    s.command(1, 'house_garage');
    s.people[0].car = null;
    s.people[1].car = car;
    car.occupants = [s.people[1]];
    s.command(2, 'garage_save', '1');
    const savedCondition = car.condition;

    car.condition = '{"fuel":0}';
    car.terminalState = 1;
    s.properties.update();
    assert.equal(car.destroyed, false);
    car.occupants = [];
    s.people[1].car = null;
    s.properties.update();
    assert.equal(car.destroyed, true);
    now += 9999;
    s.properties.update();
    assert.equal(s.cars.length, 1);
    now += 1;
    s.properties.update();
    assert.equal(s.cars.length, 2);
    assert.equal(s.cars[1].restored, savedCondition);
    assert.deepEqual(s.cars[1].position, { x: 20, y: 2, z: 20 });
    s.cars[1].position.y = -101;
    s.properties.update();
    assert.equal(s.cars[1].destroyed, true);
    now += 10000;
    s.properties.update();
    assert.equal(s.cars.length, 3);
    assert.equal(s.cars[2].restored, savedCondition);
});
