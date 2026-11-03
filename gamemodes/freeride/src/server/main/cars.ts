// Cars every player deals with: the last one each player sat in (what /repair
// and /flip work on), the admin motor pool (/car, or the car picker in admin
// mode; a newer pool car retires the older one) and the wrecks the engine
// leaves behind. The car picker's one handler routes the car lot and the
// garage to dealership.ts.
import * as dealership from '../dealership';
import * as life from '../lib/life';
import * as admin from '../lib/admin';
import * as cars from '../lib/cars';
import * as commands from '../lib/commands';
import { toast } from '../lib/notify';
import { playable, vehicleById, headingOf, emit, onClient } from '../lib/util';
import { events, onMissionChange, onStop } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';
import { Cooldown } from '../lib/cooldowns';
import { fields } from '../lib/payload';
import { emptyBut, occupied, spawnVehicle } from '../lib/vehicles';
import type { CrewTag } from '../lib/nametag';

const CAR_CHOICE_COOLDOWN_MS = 750;
// A pool car taken over by a newer one goes this long after, once empty.
const RETIRE_DELAY_MS = 4000;
// The engine supplies the splash and fall. The car goes after the death
// fade and the respawn have had time to run.
const WRECK_CLEAR_MS = 6000;

interface CarResult {
    ok: boolean;
    message: string;
    vehicleId?: number;
}

// The vehicle id each player last sat in.
const lastVehicles = new PlayerMap<number>({ clearOnMissionChange: true });
// The pool cars each player spawned.
const personalCars = new PlayerMap<Set<number>>({ clearOnMissionChange: true });
const retiringVehicles = new Map<number, ReturnType<typeof setTimeout>>();
const carChoices = new Cooldown(CAR_CHOICE_COOLDOWN_MS);

/** The car the player sits in, else the one they last left. */
export function currentOrLastVehicle(player: Player): Vehicle | null {
    return player.getVehicle() ?? vehicleById(lastVehicles.get(player.id));
}

function clearRetiring(): void {
    for (const timer of retiringVehicles.values()) {
        clearTimeout(timer);
    }

    retiringVehicles.clear();
}

function retireAfter(id: number, missionGeneration: number, delayMs = RETIRE_DELAY_MS): void {
    if (retiringVehicles.has(id)) {
        return;
    }

    const check = (): void => {
        retiringVehicles.delete(id);

        if (World.getMissionGeneration() !== missionGeneration) {
            return;
        }

        const vehicle = vehicleById(id);

        if (!vehicle) {
            return;
        }

        // A passenger can still be in the old car. Wait for the last occupant
        // instead of deleting a live multiplayer vehicle under them.
        if (occupied(vehicle)) {
            retiringVehicles.set(id, setTimeout(check, 1000));

            return;
        }

        vehicle.destroy();
    };

    retiringVehicles.set(id, setTimeout(check, delayMs));
}

function carColor(player: Player): number {
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');

    return crew?.color ? parseInt(crew.color.slice(1), 16) : 0xe8c57d;
}

function spawnPersonalCar(player: Player, model: string): CarResult {
    if (!playable(player)) {
        return { ok: false, message: 'Enter the city before choosing a car.' };
    }

    if (!cars.byModel(model)) {
        return { ok: false, message: 'That car model is unavailable.' };
    }

    const busy = life.activity(player);

    if (busy?.kind === 'race' || busy?.kind === 'duel' || busy?.kind === 'jail') {
        return { ok: false, message: life.busyMessage(player) ?? '' };
    }

    const oldVehicle = player.getVehicle();
    const { x, y, z } = oldVehicle?.position ?? player.position;
    const heading = headingOf((oldVehicle ?? player).rotation);
    // A personal car, not marked full: it starts nearly dry (shops.ts).
    const vehicle = spawnVehicle(
        model,
        { x: x + Math.cos(heading) * 3.5, y: y + 0.3, z: z - Math.sin(heading) * 3.5 },
        heading,
        { controller: player, owner: player, radar: carColor(player), fullTank: false },
    );

    if (!vehicle) {
        return { ok: false, message: 'The motor pool is busy. Try again.' };
    }

    if (!player.putInVehicle(vehicle, 0)) {
        vehicle.destroy();

        return { ok: false, message: "The new driver's seat is unavailable." };
    }

    for (const id of personalCars.get(player.id) ?? []) {
        retireAfter(id, World.getMissionGeneration());
    }

    const owned = personalCars.get(player.id) ?? new Set<number>();

    owned.add(vehicle.id);
    personalCars.set(player.id, owned);
    lastVehicles.set(player.id, vehicle.id);

    return {
        ok: true,
        vehicleId: vehicle.id,
        message: 'Your car is ready. Open a race with /race.',
    };
}

onStop(clearRetiring);

events.on('playerDisconnect', (player) => {
    for (const id of personalCars.get(player.id) ?? []) {
        const vehicle = vehicleById(id);

        if (vehicle && emptyBut(vehicle)) {
            vehicle.destroy();
        }
    }
});

onMissionChange(clearRetiring);

events.on('vehicleDestroy', (vehicle) => {
    clearTimeout(retiringVehicles.get(vehicle.id));
    retiringVehicles.delete(vehicle.id);

    for (const [playerId, id] of lastVehicles) {
        if (id === vehicle.id) {
            lastVehicles.delete(playerId);
        }
    }

    for (const ids of personalCars.values()) {
        ids.delete(vehicle.id);
    }
});

events.on('vehicleTerminal', (vehicle, state) => {
    if (state !== 2 && state !== 3) {
        return;
    }

    const id = vehicle.id;
    const missionGeneration = World.getMissionGeneration();

    setTimeout(() => {
        if (World.getMissionGeneration() !== missionGeneration) {
            return;
        }

        const current = vehicleById(id);

        if (current && current.terminalState === state) {
            current.destroy();
        }
    }, WRECK_CLEAR_MS);
});

events.on('vehiclePlayerEntered', (vehicle, player) => lastVehicles.set(player.id, vehicle.id));
events.on('vehiclePlayerExited', (vehicle, player) => lastVehicles.set(player.id, vehicle.id));

onClient('fr:car:choose', (player, payload: unknown) => {
    const { generation, id, mode } = fields(payload);

    if (
        generation !== World.getMissionGeneration() ||
        typeof id !== 'number' ||
        !Number.isInteger(id)
    ) {
        return;
    }

    if (mode === 'dealer' || mode === 'garage') {
        dealership.choose(player, { generation, id, mode });

        return;
    }

    if (mode !== 'admin' || !admin.isAdmin(player)) {
        return;
    }

    const model = cars.byId(id)?.model;

    if (!model || carChoices.take(player) > 0) {
        return;
    }

    const result = spawnPersonalCar(player, model);

    emit(player, 'fr:car:result', { id, ...result });

    if (result.ok) {
        toast(player, result.message, { tone: 'good' });
    }
});

commands.register(
    {
        name: 'car',
        aliases: ['v'],
        usage: '/car [model.i3d]',
        help: 'Admins: spawn any stock car.',
        category: 'Dev',
        hidden: true,
    },
    (player, args) => {
        if (!admin.isAdmin(player)) {
            toast(
                player,
                'Buy a car at the car lot (/dealer), call one of yours (/garage), or take one from the street.',
                { tone: 'bad' },
            );

            return;
        }

        if (!playable(player)) {
            return;
        }

        if (!args.length) {
            emit(player, 'fr:car:open', {
                generation: World.getMissionGeneration(),
                mode: 'admin',
            });

            return;
        }

        if (carChoices.take(player) > 0) {
            return;
        }

        const result = spawnPersonalCar(player, args[0]);

        toast(player, result.message, { tone: result.ok ? 'good' : 'bad' });
    },
);
