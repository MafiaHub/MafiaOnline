// Owning a car. Cars are bought at the map's car lot and kept in the
// player's garage between sessions, damage, fuel and all (Vehicle.saveState).
// One owned car is on the street at a time: /garage has it delivered to the
// nearest road for a fee, /park puts it away, /lock keeps others out of the
// driver's seat, and a wrecked car can be recovered for part of its price.
// Free car spawning is for admins (/car in main/cars.ts).
import * as maps from './lib/maps';
import * as roads from './lib/roads';
import * as cars from './lib/cars';
import * as profiles from './lib/profiles';
import * as life from './lib/life';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { Cooldown } from './lib/cooldowns';
import { events, onStop } from './lib/lifecycle';
import { toast, compass, markers, feed, name, text, COLORS } from './lib/notify';
import { PlayerMap } from './lib/players';
import { spawnVehicle } from './lib/vehicles';
import {
    MAIN_WORLD,
    distance,
    emit,
    flatDistance,
    formatMoney,
    playable,
    playerById,
    vehicleById,
    type Placement,
    type Point,
} from './lib/util';
import type { CrewTag } from './lib/nametag';
import type { OwnedCar } from './lib/profiles';

const LOT_REACH = 14;
const MAX_OWNED = 3;
const DELIVERY_FEE = 75;
const DELIVERY_COOLDOWN_MS = 60_000;
const PARK_REACH = 10;
const RECOVER_FRACTION = 0.3;
const SELL_FRACTION = 0.5;
// What a car the catalogue does not price counts as, to recover it.
const UNPRICED = 1000;
// How far from the nearest road a car is still brought.
const ROAD_REACH = 120;
const ROADSIDE = 3;
const LOT_COLOR = 0xe8c57d;
const COMPASS_PRIORITY = 20;
const COMPASS_MS = 30_000;
const PICKER_SLOT_BASE = 1000;
const BLOCKED: ReadonlySet<string> = new Set(['race', 'duel', 'jail']);

/** The garage car a player has on the street. */
interface OutCar {
    vehicleId: number;
    slot: number;
    locked: boolean;
}

/** What the car picker (client/car_picker.ts) asked for; main/cars.ts checks it and routes it here. */
export interface CarChoice {
    /** The mission generation the picker was opened in. */
    generation: number;
    id: number;
    mode: 'dealer' | 'garage';
}

/** How a choice went, for the picker and a toast. */
interface ChoiceResult {
    ok: boolean;
    message: string;
}

/** A garage car parked at a home (server/housing). */
export interface ParkedCar {
    slot: number;
    model: string;
    home: string | undefined;
    totaled: boolean;
}

/** The home whose parking spot the vehicle stands on, or null. */
export type HomeSpot = (player: Player, vehicle: Vehicle) => string | null;

const out = new PlayerMap<OutCar>({ clearOnMissionChange: true });
const deliveries = new Cooldown(DELIVERY_COOLDOWN_MS);

// Each map's lot is on the road beside one of its landmarks.
const LOTS: Readonly<Record<maps.MapId, maps.LandmarkKey>> = {
    freeride: 'pump',
    freeride_extended: 'bertone',
    freekrajina: 'lowlands',
};

function lot(): { name: string; position: Point } | null {
    const map = maps.current();

    if (!map) {
        return null;
    }

    const place = maps.landmark(LOTS[map.id]);
    const graph = roads.forMap(map);

    return {
        name: `the car lot at ${place.name}`,
        position: graph.crossings[graph.nearestCrossing(place.position)],
    };
}

function garage(player: Player): OwnedCar[] {
    return profiles.get(player)?.cars ?? [];
}

function saveGarage(player: Player, list: OwnedCar[]): void {
    profiles.setField(player, 'cars', list);
}

function nearLot(player: Player): boolean {
    const place = lot();

    return Boolean(place && flatDistance(player.position, place.position) <= LOT_REACH);
}

function busy(player: Player): string | null {
    const current = life.activity(player);

    return current && BLOCKED.has(current.kind) ? life.busyMessage(player) : null;
}

function recoverCost(model: string): number {
    return Math.round((cars.byModel(model)?.price ?? UNPRICED) * RECOVER_FRACTION);
}

// Puts the car that is out back into the garage with its condition. Someone
// else sitting in it stops that, unless the owner is leaving the server: then
// they are put out, so the car never stays on the street while it is also
// in the garage.
function store(player: Player, force = false): boolean {
    const entry = out.get(player.id);

    if (!entry) {
        return true;
    }

    const vehicle = vehicleById(entry.vehicleId);
    const others = vehicle
        ? vehicle
              .getOccupants()
              .filter(
                  (occupant): occupant is Player => occupant !== null && occupant.id !== player.id,
              )
        : [];

    if (others.length && !force) {
        return false;
    }

    for (const occupant of others) {
        occupant.removeFromVehicle();
    }

    const list = garage(player).slice();

    if (vehicle && list[entry.slot]) {
        const state = vehicle.saveState();

        if (state) {
            list[entry.slot] = { ...list[entry.slot], state };
        }

        saveGarage(player, list);
    }

    out.delete(player.id);

    if (vehicle) {
        if (player.getVehicle()?.id === vehicle.id) {
            player.removeFromVehicle();
        }

        vehicle.destroy();
    }

    return true;
}

/** Where a delivered car stands: at a home's kerb, or by the nearest road. */
function deliverySpot(player: Player, at: Placement | null): Placement | null {
    // A home's kerb (server/housing) needs no road search.
    if (at) {
        return {
            position: { x: at.position.x, y: at.position.y + 0.6, z: at.position.z },
            heading: at.heading,
        };
    }

    const spot = roads.current().nearestRoadPoint(player.position);

    if (!spot || spot.gap > ROAD_REACH) {
        return null;
    }

    const { position, direction } = spot;

    return {
        position: {
            x: position.x + direction.z * ROADSIDE,
            y: position.y + 0.6,
            z: position.z - direction.x * ROADSIDE,
        },
        heading: Math.atan2(direction.x, direction.z),
    };
}

function radarColor(player: Player): number {
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');

    return crew?.color ? parseInt(crew.color.slice(1), 16) : LOT_COLOR;
}

function deliver(player: Player, slot: number, fee: number, at: Placement | null = null): boolean {
    if (!garage(player)[slot] || garage(player)[slot].totaled) {
        return false;
    }

    if (!store(player)) {
        toast(player, 'Someone is still sitting in your other car.', { tone: 'bad' });

        return false;
    }

    // Read after store(): calling round the car that is out brings it back
    // as it was put away, not as it was last parked.
    const owned = garage(player)[slot];
    const spot = deliverySpot(player, at);

    if (!spot) {
        toast(player, 'No road nearby to bring the car to.', { tone: 'bad' });

        return false;
    }

    if (fee && !player.trySpendMoney(fee)) {
        toast(player, `Delivery costs ${formatMoney(fee)}.`, { tone: 'bad' });

        return false;
    }

    // A saved condition is applied before the first native report; a new
    // car comes with a full tank.
    const vehicle = spawnVehicle(owned.model, spot.position, spot.heading, {
        controller: player,
        owner: player,
        radar: radarColor(player),
        condition: owned.state,
        state: { 'fr.locked': true },
    });

    if (!vehicle) {
        if (fee) {
            player.giveMoney(fee);
        }

        toast(player, 'The car could not be brought out. Try again.', { tone: 'bad' });

        return false;
    }

    out.set(player.id, { vehicleId: vehicle.id, slot, locked: true });

    if (owned.home) {
        const list = garage(player).slice();
        const { home: _home, ...rest } = list[slot];

        list[slot] = rest;
        saveGarage(player, list);
    }

    if (!at) {
        deliveries.start(player);
    }

    compass(player, 'garage', COMPASS_PRIORITY, { vehicleId: vehicle.id });
    setTimeout(() => compass(player, 'garage', COMPASS_PRIORITY, null), COMPASS_MS);

    return true;
}

// Homes (server/housing) park a car at their kerb: it goes into the garage
// like any other, condition and all, tagged with the home, and comes out
// there again for nothing.
let homeSpot: HomeSpot = () => null;

export function setHomeSpot(handler: HomeSpot): void {
    homeSpot = handler;
}

/** The player's garage car on the street. */
export function carOut(player: Player): { vehicle: Vehicle; slot: number; model: string } | null {
    const entry = out.get(player.id);
    const vehicle = entry ? vehicleById(entry.vehicleId) : null;

    return entry && vehicle
        ? { vehicle, slot: entry.slot, model: garage(player)[entry.slot]?.model ?? vehicle.model }
        : null;
}

/** Puts the car that is out away at the home; false while someone else sits in it. */
export function parkAtHome(player: Player, homeId: string): boolean {
    const entry = out.get(player.id);

    if (!entry) {
        return false;
    }

    const { slot } = entry;

    if (!store(player)) {
        return false;
    }

    const list = garage(player).slice();

    if (list[slot]) {
        list[slot] = { ...list[slot], home: homeId };
    }

    saveGarage(player, list);

    return true;
}

/** The player's cars parked at the home and in the garage (not wrecked, not out). */
export function parkedAt(player: Player, homeId: string): ParkedCar[] {
    return garage(player)
        .map((owned, slot) => ({
            slot,
            model: owned.model,
            home: owned.home,
            totaled: owned.totaled,
        }))
        .filter(
            (owned) =>
                owned.home === homeId && !owned.totaled && out.get(player.id)?.slot !== owned.slot,
        );
}

/** Brings a car parked at a home out at its kerb, for nothing. */
export function fromHome(player: Player, slot: number, pose: Placement): boolean {
    return garage(player)[slot]?.home !== undefined && deliver(player, slot, 0, pose);
}

function openPicker(player: Player, mode: CarChoice['mode']): void {
    const list = garage(player);
    const offers =
        mode === 'dealer'
            ? cars.forSale().map((entry) => ({
                  id: entry.id,
                  model: entry.model,
                  price: entry.price,
                  owned: list.some(
                      (owned) => owned.model.toLowerCase() === entry.model.toLowerCase(),
                  ),
              }))
            : list.map((owned, slot) => ({
                  id: PICKER_SLOT_BASE + slot,
                  model: owned.model,
                  totaled: Boolean(owned.totaled),
                  recover: owned.totaled ? recoverCost(owned.model) : 0,
              }));

    emit(player, 'fr:car:open', {
        generation: World.getMissionGeneration(),
        mode,
        offers,
        fee: DELIVERY_FEE,
        room: MAX_OWNED - list.length,
    });
}

function buy(player: Player, entry: cars.CarEntry): ChoiceResult {
    const list = garage(player);

    if (!nearLot(player)) {
        return { ok: false, message: 'Come to the car lot to buy.' };
    }

    if (list.length >= MAX_OWNED) {
        return {
            ok: false,
            message: `Your garage holds ${MAX_OWNED} cars. Sell one with /sellcar first.`,
        };
    }

    if (entry.price === null) {
        return { ok: false, message: 'That car is not for sale.' };
    }

    if (!player.trySpendMoney(entry.price)) {
        return {
            ok: false,
            message: `That car costs ${formatMoney(entry.price)}; you have ${formatMoney(player.money)}.`,
        };
    }

    saveGarage(player, [
        ...list,
        { model: entry.model, state: null, totaled: false, bought: Date.now() },
    ]);

    profiles.bump(player, 'carsBought');
    feed('car', [name(player, COLORS.gold), text(' drove a new car off the lot')]);
    deliver(player, list.length, 0);

    return {
        ok: true,
        message: `Yours for ${formatMoney(entry.price)}. It is parked on the road by the lot; /lock and /park look after it.`,
    };
}

function fromGarage(player: Player, slot: number): ChoiceResult {
    const owned = garage(player)[slot];

    if (!owned) {
        return { ok: false, message: 'That car is not in your garage.' };
    }

    const wait = deliveries.remaining(player);

    if (wait > 0) {
        return {
            ok: false,
            message: `The driver is on his way back; try again in ${Math.ceil(wait / 1000)} s.`,
        };
    }

    if (owned.totaled) {
        const cost = recoverCost(owned.model);

        if (!player.trySpendMoney(cost)) {
            return { ok: false, message: `Recovering the wreck costs ${formatMoney(cost)}.` };
        }

        const list = garage(player).slice();

        list[slot] = { ...owned, state: null, totaled: false };
        saveGarage(player, list);
        toast(player, `The wreck was hauled off and rebuilt for ${formatMoney(cost)}.`, {
            tone: 'good',
        });
    }

    const fee = nearLot(player) ? 0 : DELIVERY_FEE;

    if (!deliver(player, slot, fee)) {
        return { ok: false, message: 'The car could not be brought out.' };
    }

    return {
        ok: true,
        message: fee
            ? `Delivered to the nearest road for ${formatMoney(fee)}.`
            : 'Your car is out front.',
    };
}

/** A choice from the car lot or the garage; main/cars.ts routes fr:car:choose here. */
export function choose(player: Player, request: CarChoice): void {
    const reply = (result: ChoiceResult): void => {
        emit(player, 'fr:car:result', { id: request.id, ...result });
        toast(player, result.message, { tone: result.ok ? 'good' : 'bad' });
    };

    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) {
        reply({ ok: false, message: 'Step out onto the street first.' });

        return;
    }

    const problem = busy(player);

    if (problem) {
        reply({ ok: false, message: problem });

        return;
    }

    if (request.mode === 'dealer') {
        const entry = cars.byId(request.id);

        reply(entry ? buy(player, entry) : { ok: false, message: 'That car is not for sale.' });

        return;
    }

    reply(fromGarage(player, request.id - PICKER_SLOT_BASE));
}

// A locked car keeps everyone but its owner and their crew out of the driver's
// seat, whether they open its door or slide across from the passenger's.
function keepOutOfLockedCar(vehicle: Vehicle, player: Player, seat: number): void {
    if (seat !== 0) {
        return;
    }

    for (const [ownerId, entry] of out) {
        if (entry.vehicleId !== vehicle.id || ownerId === player.id || !entry.locked) {
            continue;
        }

        const owner = playerById(ownerId);
        const ownerCrew: CrewTag | null | undefined = owner?.state.get('fr.crew');
        const playerCrew: CrewTag | null | undefined = player.state.get('fr.crew');

        if (ownerCrew?.tag && ownerCrew.tag === playerCrew?.tag) {
            return;
        }

        player.removeFromVehicle();
        toast(player, "It's locked.", { tone: 'bad', seconds: 2 });

        if (owner) {
            toast(owner, `${player.nickname} tried the door of your car.`, { seconds: 4 });
        }

        return;
    }
}

events.on('vehiclePlayerEntered', (vehicle, player, { seat }) =>
    keepOutOfLockedCar(vehicle, player, seat),
);

events.on('vehiclePlayerSeatChanged', (vehicle, player, { seat }) =>
    keepOutOfLockedCar(vehicle, player, seat),
);

events.on('vehicleTerminal', (vehicle, state) => {
    for (const [ownerId, entry] of out) {
        if (entry.vehicleId !== vehicle.id) {
            continue;
        }

        const player = playerById(ownerId);

        out.delete(ownerId);

        if (!player) {
            return;
        }

        const list = garage(player).slice();

        if (list[entry.slot]) {
            list[entry.slot] = { ...list[entry.slot], state: null, totaled: true };
        }

        saveGarage(player, list);
        toast(
            player,
            state === 1
                ? 'Your car is a burnt-out wreck. /garage can have it rebuilt.'
                : 'Your car is lost. /garage can have it recovered.',
            { tone: 'bad' },
        );

        return;
    }
});

events.on('vehicleDestroy', (vehicle) => {
    for (const [ownerId, entry] of out) {
        if (entry.vehicleId === vehicle.id) {
            out.delete(ownerId);
        }
    }
});

commands.register(
    {
        name: 'dealer',
        aliases: ['buycar', 'carlot'],
        usage: '/dealer',
        help: 'Buy a car at the car lot.',
        category: 'Cars',
    },
    (player) => {
        const place = lot();

        if (!place) {
            toast(player, 'There is no car lot on this map.', { tone: 'bad' });

            return;
        }

        if (!nearLot(player)) {
            compass(player, 'garage', COMPASS_PRIORITY, { position: place.position });
            markers(player, 'garage', [
                {
                    id: 'lot',
                    position: place.position,
                    label: 'CAR LOT',
                    color: COLORS.gold,
                    beacon: true,
                    primary: true,
                },
            ]);

            toast(player, `Cars are sold at ${place.name}. Your compass points there.`);

            return;
        }

        markers(player, 'garage', []);
        compass(player, 'garage', COMPASS_PRIORITY, null);
        openPicker(player, 'dealer');
    },
);

commands.register(
    {
        name: 'garage',
        aliases: ['mycar', 'cars'],
        usage: '/garage',
        help: 'Have one of your cars brought to you.',
        category: 'Cars',
    },
    (player) => {
        if (!garage(player).length) {
            toast(
                player,
                'Your garage is empty. Buy a car with /dealer, or take one from the street.',
            );

            return;
        }

        openPicker(player, 'garage');
    },
);

commands.register(
    { name: 'park', usage: '/park', help: 'Put your car away in your garage.', category: 'Cars' },
    (player) => {
        const vehicle = carOut(player)?.vehicle;

        if (!vehicle) {
            toast(player, 'None of your cars is out.');

            return;
        }

        if (distance(player.position, vehicle.position) > PARK_REACH) {
            toast(player, 'Stand by your car to park it.', { tone: 'bad' });

            return;
        }

        const home = homeSpot(player, vehicle);

        if (home) {
            toast(
                player,
                parkAtHome(player, home)
                    ? "Into your home's garage, as it is. G on the parking spot brings it out again."
                    : 'Someone is still sitting in it.',
                { tone: 'good' },
            );

            return;
        }

        toast(
            player,
            store(player)
                ? 'Parked. It will be in your garage next time.'
                : 'Someone is still sitting in it.',
            { tone: 'good' },
        );
    },
);

commands.register(
    {
        name: 'lock',
        aliases: ['unlock'],
        usage: '/lock',
        help: 'Lock or unlock your car.',
        category: 'Cars',
    },
    (player) => {
        const entry = out.get(player.id);
        const vehicle = entry ? vehicleById(entry.vehicleId) : null;

        if (!entry || !vehicle) {
            toast(player, 'None of your cars is out.');

            return;
        }

        entry.locked = !entry.locked;
        vehicle.state.set('fr.locked', entry.locked);
        toast(player, entry.locked ? 'Locked.' : 'Unlocked.', { seconds: 2 });
    },
);

function garageLine(list: OwnedCar[]): string {
    const cars = list.map(
        (owned, index) =>
            `${index + 1}. ${owned.model.replace(/00\.i3d$/i, '')}${owned.totaled ? ' (wrecked)' : ''}`,
    );

    return `Your garage: ${cars.join(', ') || 'empty'}. /sellcar <number>`;
}

commands.register(
    {
        name: 'sellcar',
        usage: '/sellcar <number>',
        help: 'Sell a car from your garage at the lot for half its price.',
        category: 'Cars',
    },
    (player, args) => {
        const list = garage(player);
        const slot = Number(args[0]) - 1;
        const owned = list[slot];

        if (!owned) {
            player.sendMessage(garageLine(list), 0xd9cdb4);

            return;
        }

        if (!nearLot(player)) {
            toast(player, 'Sell cars at the car lot.', { tone: 'bad' });

            return;
        }

        if (out.get(player.id)?.slot === slot && !store(player)) {
            toast(player, 'Someone is sitting in that car.', { tone: 'bad' });

            return;
        }

        const entry = out.get(player.id);

        if (entry && entry.slot > slot) {
            entry.slot--;
        }

        const price = owned.totaled
            ? 0
            : Math.round((cars.byModel(owned.model)?.price ?? 0) * SELL_FRACTION);

        saveGarage(
            player,
            list.filter((_, index) => index !== slot),
        );

        if (price) {
            player.giveMoney(price);
        }

        toast(player, price ? `Sold for ${formatMoney(price)}.` : 'The wreck went for scrap.', {
            tone: 'good',
        });
    },
);

menu.provide('garage', (player) => ({
    lot: lot()?.name ?? null,
    near: nearLot(player),
    fee: DELIVERY_FEE,
    max: MAX_OWNED,
    cars: garage(player).map((owned, slot) => ({
        slot: slot + 1,
        model: owned.model,
        totaled: Boolean(owned.totaled),
        out: out.get(player.id)?.slot === slot,
        home: owned.home ?? null,
    })),
}));

events.on('playerDisconnect', (player) => {
    store(player, true);
});

onStop(() => {
    for (const player of World.getPlayers()) {
        store(player, true);
    }
});
