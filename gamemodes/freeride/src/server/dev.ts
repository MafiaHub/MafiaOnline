// Development commands for testing combat, cars and the HUD. They are off on
// a public server; see lib/devmode.ts for turning them on. The server
// console runs any of them on a player: "fr as <player> <action> ...".
import * as commands from './lib/commands';
import * as devmode from './lib/devmode';
import * as serverConsole from './lib/console';
import * as log from './lib/log';
import * as jail from './jail';
import { toast } from './lib/notify';
import { emit, playable, findPlayer, vehicleById, headingOf, ahead } from './lib/util';
import { events, onStop } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import type { WeaponGrant } from './lib/life';

type Action = (player: Player, args: string[]) => string;

const testObjects = new PlayerMap<MapObject>();

function testKit(player: Player, big: string): boolean {
    const large: WeaponGrant =
        big === 'shotgun'
            ? { weaponId: 11, loaded: 8, reserve: 32 }
            : big === 'thompson'
              ? { weaponId: 10, loaded: 50, reserve: 150 }
              : { weaponId: 4 };

    return player.setInventory(
        [
            { weaponId: 3 }, // knife: melee replay
            { weaponId: 6, loaded: 6, reserve: 36 },
            large,
            { weaponId: 15, loaded: 3 }, // grenades
        ],
        large.weaponId,
    );
}

function lastCar(player: Player): Vehicle | null {
    return (
        player.getVehicle() ??
        vehicleById(
            World.getVehicles()
                .filter((vehicle) => vehicle.state.get('fr.owner') === player.nickname)
                .pop()?.id,
        )
    );
}

const actions: Record<string, Action> = {
    weapons(player, [variant = 'bat']) {
        if (!['bat', 'shotgun', 'thompson'].includes(variant)) {
            return 'Use /dev weapons bat|shotgun|thompson';
        }

        return testKit(player, variant)
            ? `Test kit: knife, Colt, ${variant}, grenades`
            : 'Could not equip the test kit';
    },
    fists(player) {
        return player.setInventory([]) ? 'Unarmed' : 'Could not holster';
    },
    drop(player, [id]) {
        const pickup = id === undefined ? player.dropWeapon() : player.dropWeapon(Number(id));

        return pickup
            ? `Dropped weapon ${pickup.weaponId} as pickup ${pickup.id}`
            : 'Weapon not held, or no room for another pickup';
    },
    model(player, [file]) {
        if (!file) {
            return `Current model: ${player.model}`;
        }

        return player.setModel(file) ? `Model set to ${file}` : 'Invalid model filename';
    },
    wanted(player, [level]) {
        const value = Number(level);

        return Number.isInteger(value) && Police.setWanted(player, value)
            ? `Wanted level ${value}`
            : 'Use /dev wanted 0-3 (needs the simulation host)';
    },
    hud(player, [action = 'sound', value]) {
        emit(player, 'fr:dev:hud', { action, value });

        return `HUD test: ${action}`;
    },
    opacity(player, [value]) {
        const car = lastCar(player);
        const opacity = Number(value);

        return car && Number.isFinite(opacity) && car.setOpacity(opacity)
            ? `Opacity ${opacity}`
            : 'Use /dev opacity 0-1 near your car';
    },
    carstate(player) {
        const car = lastCar(player);

        if (!car) {
            return 'No car';
        }

        const { x, y, z } = car.velocity;

        return (
            `Car ${car.id}: ${Math.round(Math.hypot(x, y, z) * 3.6)} km/h, gear ${car.gear}, ` +
            `fuel ${car.fuel.toFixed(1)}/${car.fuelTankCapacity.toFixed(1)}, engine ${car.engineOn ? 'on' : 'off'}`
        );
    },
    cardamage(player) {
        const damage = lastCar(player)?.getDamageState();

        if (!damage) {
            return 'No native damage reported yet';
        }

        const broken = (parts: readonly { flags: number }[], bit: number): number =>
            parts.filter((part) => (part.flags & bit) !== 0).length;

        return (
            `Damage revision ${damage.revision}, body ${damage.bodyDamage.toFixed(1)}, ` +
            `glass/parts ${broken(damage.zones, 1)}, tires ${broken(damage.wheels, 0x400)}`
        );
    },
    // Straight to a cell, as after an arrest at this wanted level.
    jail(player, [level = '1']) {
        return jail.sentence(player, Math.max(1, Number(level) || 1))
            ? 'Locked up'
            : 'Could not lock you up here';
    },
    // A map object four metres ahead, for collision tests; the next one replaces it.
    object(player, [model]) {
        if (typeof MapObject === 'undefined') {
            return 'This server has no MapObject';
        }

        if (!model) {
            return 'Use /dev object <model.4ds> (off removes it)';
        }

        testObjects.get(player.id)?.destroy();
        testObjects.delete(player.id);

        if (model === 'off') {
            return 'Removed';
        }

        const heading = headingOf(player.rotation);
        const object = MapObject.create(
            model,
            ahead(player.position, heading, 4),
            { x: 0, y: heading, z: 0 },
            { virtualWorld: player.virtualWorld },
        );

        if (!object) {
            return `Could not create ${model}`;
        }

        testObjects.set(player.id, object);

        return `Object ${object.id}: ${model}`;
    },
    money(player, [amount, who]) {
        const target = who ? findPlayer(who) : player;

        return target && Number.isInteger(Number(amount)) && target.giveMoney(Number(amount))
            ? `${target.nickname} has $${target.money}`
            : 'Use /dev money <amount> [player]';
    },
};

const actionList = Object.keys(actions).join('|');

function actionNamed(name: string | undefined): Action | null {
    return name !== undefined && Object.hasOwn(actions, name) ? actions[name] : null;
}

commands.register(
    {
        name: 'dev',
        usage: '/dev weapons|fists|drop|model|wanted|hud|opacity|carstate|cardamage|jail|object|money',
        help: 'Development tools (FREERIDE_DEV=1 or console: fr dev on).',
        category: 'Dev',
        hidden: true,
    },
    (player, [action, ...args]) => {
        if (!devmode.isOn()) {
            toast(player, 'Development commands are off on this server.', { tone: 'bad' });

            return;
        }

        const run = actionNamed(action);

        if (!run) {
            player.sendMessage(`/dev ${actionList}`, 0xd9cdb4);

            return;
        }

        if (!playable(player) && action !== 'hud') {
            return;
        }

        player.sendMessage(run(player, args), 0xd9cdb4);
    },
);

// fr as <player> <action> ...: the console has no player of its own.
serverConsole.action('fr', 'as', ([who, action, ...args]) => {
    if (who === undefined || action === undefined) {
        return false;
    }

    const target = findPlayer(who);
    const run = actionNamed(action);

    log.info(
        target
            ? run
                ? run(target, args)
                : `Actions: ${actionList}`
            : `No connected player "${who}"`,
    );

    return true;
});

events.on('playerDisconnect', (player) => testObjects.get(player.id)?.destroy());

onStop(() => {
    for (const object of testObjects.values()) {
        object.destroy();
    }

    testObjects.clear();
});
