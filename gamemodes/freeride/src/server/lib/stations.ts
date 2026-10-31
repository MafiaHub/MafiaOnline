// Job stations: where a job is joined and where its vehicles wait. A station
// has a sign-on point on foot (the retail mission arrow spinning over the
// spot, with a label: stand under it and press G to go on or off duty) and
// parking spots its vehicles return to. Both show on the city map. Anyone may
// ride in a station's vehicle, but only a player on that job drives it. A
// wreck, or a vehicle left empty away from its spot, is replaced at the spot.
import * as jobkey from './jobkey';
import { toast, compass, markers, markersAll, type Marker } from './notify';
import {
    MAIN_WORLD,
    flatDistance,
    distance,
    playable,
    playerById,
    vehicleById,
    inFreeRide,
    type Placement,
    type Point,
} from './util';
import { events, every, onStop } from './lifecycle';
import { PlayerMap } from './players';
import { occupied, spawnVehicle } from './vehicles';

export const SIGN_ON_RADIUS = 2.2;
const TICK_MS = 250;
const UPKEEP_MS = 5000;
const RESPAWN_MS = 20_000;
const ABANDON_MS = 3 * 60_000;
const ABANDON_REACH = 30;
const CREW_NEARBY = 80;
const SPOT_CLEAR = 4;
const INTRO_EVERY_MS = 30_000;
const COMPASS_PRIORITY = 40;
const STEAL_UNDO_MS = 3000;
const FLEET_POSITION_STEP = 10;

/** Where a station is on the loaded map. */
export interface StationPlace {
    signOn: Point;
    spots: Placement[];
}

export interface StationDefinition {
    id: string;
    title: string;
    /** Who drives its vehicles, plural: "medics". */
    crew: string;
    mapLabel: string;
    color: string;
    /** City-map blip colour of its vehicles (0xRRGGBB). */
    radar: number;
    model: string;
    vehicleLabel: string;
    /** Shown to someone who walks up to the sign-on point off duty. */
    pitch: string;
    /** Where the station is on this map, or null when it has none here. */
    place(): StationPlace | null;
    available(): boolean;
    onDuty(player: Player): boolean;
    start(player: Player): void;
    stop(player: Player): void;
    onDrive?(player: Player, vehicle: Vehicle): void;
    onLeave?(player: Player, vehicle: Vehicle): void;
    /** True lets someone off duty take this vehicle's wheel. */
    mayDrive?(player: Player, vehicle: Vehicle): boolean;
}

interface Slot {
    vehicleId: number | null;
    missingSince: number | null;
    awaySince: number | null;
    wreckedAt: number | null;
}

export interface Station extends StationDefinition {
    /** The station's place when Free Ride runs a map that has it. */
    here(): StationPlace | null;
    vehicles(): Vehicle[];
    isVehicle(vehicle: Vehicle | null | undefined): boolean;
    nearestVehicle(position: Point): { vehicle: Vehicle; gap: number } | null;
    /** Points the player at the sign-on point; asked again, it stops. False when it stopped or there is no point here. */
    guide(player: Player): boolean;
    stopGuiding(player: Player): void;
    /** Re-sends this job's vehicles to whoever is on duty now. */
    refresh(): void;
}

interface StationState {
    slots: Slot[];
    fleetShownTo: Set<number>;
    lastFleet: string;
}

const stations: Station[] = [];
const state = new Map<Station, StationState>();
// The sign-on point the player stands on and the duty state the prompt was written for.
const standing = new PlayerMap<{ station: Station; onDuty: boolean }>();
const introducedAt = new PlayerMap<number>();
// Station id whose sign-on point the compass shows the player.
const guiding = new PlayerMap<string>();
// Who last left a station vehicle's wheel, so a refused steal can put them back.
const lastDrivers = new Map<number, { playerId: number; at: number }>();
let lastPoints = '';

function streetPlayers(): Player[] {
    return World.getPlayers().filter(
        (player) => playable(player) && player.virtualWorld === MAIN_WORLD,
    );
}

function spotClear(spot: Placement): boolean {
    return !World.getVehicles().some(
        (vehicle) => flatDistance(vehicle.position, spot.position) < SPOT_CLEAR,
    );
}

function stateOf(station: Station): StationState {
    return state.get(station)!;
}

export function define(definition: StationDefinition): Station {
    const station: Station = {
        ...definition,
        here: () => (inFreeRide() && station.available() ? station.place() : null),
        vehicles: () =>
            stateOf(station)
                .slots.map((slot) => vehicleById(slot.vehicleId))
                .filter((vehicle): vehicle is Vehicle => vehicle !== null),
        isVehicle: (vehicle) =>
            Boolean(vehicle) &&
            stateOf(station).slots.some((slot) => slot.vehicleId === vehicle!.id),
        nearestVehicle: (position) =>
            station
                .vehicles()
                .map((vehicle) => ({ vehicle, gap: distance(vehicle.position, position) }))
                .sort((a, b) => a.gap - b.gap)[0] ?? null,
        guide(player) {
            const place = station.here();

            if (!place || guiding.get(player.id) === station.id) {
                station.stopGuiding(player);

                return false;
            }

            const previous = guiding.get(player.id);

            if (previous !== undefined) {
                compass(player, `station:${previous}`, COMPASS_PRIORITY, null);
            }

            guiding.set(player.id, station.id);
            compass(player, `station:${station.id}`, COMPASS_PRIORITY, { position: place.signOn });

            return true;
        },
        stopGuiding(player) {
            if (guiding.get(player.id) === station.id) {
                guiding.delete(player.id);
            }

            compass(player, `station:${station.id}`, COMPASS_PRIORITY, null);
        },
        refresh: () => presentFleet(station),
    };

    stations.push(station);
    state.set(station, { slots: [], fleetShownTo: new Set(), lastFleet: '' });

    return station;
}

function presentPoints(): void {
    const items: Marker[] = [];

    for (const station of stations) {
        const place = station.here();

        if (place) {
            items.push({
                id: station.id,
                position: place.signOn,
                label: station.title.toUpperCase(),
                color: station.color,
                model: 'sipka.i3d',
                range: 250,
                near: 60,
                hint: 'STAND UNDER THE ARROW · PRESS G',
                map: { icon: 'square', label: station.mapLabel, size: 6 },
            });
        }
    }

    const payload = JSON.stringify(items);

    if (payload === lastPoints) {
        return;
    }

    lastPoints = payload;
    markersAll('stations', items);
}

// Where a vehicle is, to the nearest few metres: the client shows this when
// the vehicle is not streamed to it, and the coarse step keeps it from
// resending for every metre driven.
export function roughPosition(vehicle: Vehicle): Point {
    const { x, y, z } = vehicle.position;
    const step = (value: number): number =>
        Math.round(value / FLEET_POSITION_STEP) * FLEET_POSITION_STEP;

    return { x: step(x), y, z: step(z) };
}

// Players on the job see where its vehicles are, on the map and up close.
function presentFleet(station: Station): void {
    const fleet = stateOf(station);
    const items: Marker[] = station.vehicles().map((vehicle, index) => ({
        id: `v${index}`,
        vehicleId: vehicle.id,
        position: roughPosition(vehicle),
        label: station.vehicleLabel,
        color: station.color,
        range: 40,
        map: { icon: 'triangle', size: 4 },
    }));
    const payload = JSON.stringify(items);
    const crew = new Set(
        World.getPlayers()
            .filter((player) => station.onDuty(player))
            .map((player) => player.id),
    );

    for (const id of fleet.fleetShownTo) {
        const player = crew.has(id) ? null : playerById(id);

        if (player) {
            markers(player, `fleet:${station.id}`, []);
        }
    }

    for (const id of crew) {
        if (fleet.fleetShownTo.has(id) && payload === fleet.lastFleet) {
            continue;
        }

        const player = playerById(id);

        if (player) {
            markers(player, `fleet:${station.id}`, items);
        }
    }

    fleet.fleetShownTo = crew;
    fleet.lastFleet = payload;
}

function spawnAt(station: Station, slot: Slot, spot: Placement): void {
    const vehicle = spawnVehicle(station.model, spot.position, spot.heading, {
        engineOff: true,
        radar: station.radar,
        state: { 'fr.station': station.id },
    });

    if (!vehicle) {
        return;
    }

    slot.vehicleId = vehicle.id;
    slot.missingSince = null;
    slot.awaySince = null;
    slot.wreckedAt = null;
}

// Takes the slot's vehicle away. An occupied one stays unless `evict`, which
// puts everyone out first: a station that goes away leaves no stray vehicle
// anyone could drive.
function retire(slot: Slot, evict = false): void {
    const vehicle = vehicleById(slot.vehicleId);

    if (vehicle && evict) {
        for (const occupant of vehicle.getOccupants()) {
            occupant?.removeFromVehicle();
        }
    }

    if (vehicle && (evict || !occupied(vehicle))) {
        vehicle.destroy();
    }

    slot.vehicleId = null;
    slot.awaySince = null;
    slot.wreckedAt = null;
}

function upkeepSlot(
    station: Station,
    slot: Slot,
    spot: Placement,
    now: number,
    anyone: boolean,
): void {
    const vehicle = vehicleById(slot.vehicleId);

    if (
        vehicle &&
        slot.wreckedAt !== null &&
        now - slot.wreckedAt >= RESPAWN_MS &&
        !occupied(vehicle)
    ) {
        retire(slot);
        slot.missingSince = now - RESPAWN_MS;

        return;
    }

    if (!vehicle) {
        slot.vehicleId = null;
        slot.missingSince ??= now;

        // The first vehicles are there at once, a replacement after a while.
        if (anyone && now - slot.missingSince >= RESPAWN_MS && spotClear(spot)) {
            spawnAt(station, slot, spot);
        }

        return;
    }

    const away = flatDistance(vehicle.position, spot.position) > ABANDON_REACH;
    const crewNearby = World.getPlayers().some(
        (player) =>
            station.onDuty(player) && distance(player.position, vehicle.position) < CREW_NEARBY,
    );

    if (!away || occupied(vehicle) || crewNearby) {
        slot.awaySince = null;

        return;
    }

    slot.awaySince ??= now;

    if (now - slot.awaySince >= ABANDON_MS) {
        // Towed back: the replacement waits at the spot right away.
        retire(slot);
        slot.missingSince = now - RESPAWN_MS;
    }
}

function upkeep(): void {
    if (!World.isReady()) {
        return;
    }

    const now = Date.now();
    const anyone = streetPlayers().length > 0;

    for (const station of stations) {
        const fleet = stateOf(station);
        const place = station.here();

        if (!place) {
            for (const slot of fleet.slots) {
                retire(slot, true);
            }

            fleet.slots = [];
            presentFleet(station);
            continue;
        }

        while (fleet.slots.length < place.spots.length) {
            fleet.slots.push({
                vehicleId: null,
                missingSince: 0,
                awaySince: null,
                wreckedAt: null,
            });
        }

        place.spots.forEach((spot, index) =>
            upkeepSlot(station, fleet.slots[index], spot, now, anyone),
        );

        presentFleet(station);
    }

    presentPoints();
}

function stationAt(player: Player): Station | null {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD || player.getVehicle()) {
        return null;
    }

    for (const station of stations) {
        const place = station.here();

        if (
            place &&
            flatDistance(player.position, place.signOn) <= SIGN_ON_RADIUS &&
            Math.abs(player.position.y - place.signOn.y) < 3
        ) {
            return station;
        }
    }

    return null;
}

function tick(): void {
    const now = Date.now();

    for (const player of World.getPlayers()) {
        const station = stationAt(player);
        const previous = standing.get(player.id) ?? null;
        const onDuty = station ? station.onDuty(player) : false;

        // The prompt follows duty changed elsewhere too (/fire, an arrest).
        if (station === previous?.station && onDuty === previous?.onDuty) {
            continue;
        }

        const arrived = station !== previous?.station;

        if (!station) {
            standing.delete(player.id);
            jobkey.prompt(player, null, null, 'station');
            continue;
        }

        standing.set(player.id, { station, onDuty });
        const title = station.title.toUpperCase();

        jobkey.prompt(
            player,
            onDuty ? `[G] CLOCK OFF · ${title}` : `[G] SIGN ON · ${title}`,
            null,
            'station',
        );

        station.stopGuiding(player);

        if (arrived && !onDuty && now - (introducedAt.get(player.id) ?? 0) >= INTRO_EVERY_MS) {
            introducedAt.set(player.id, now);
            toast(player, station.pitch, { title: station.title, tone: 'gold', seconds: 8 });
        }
    }
}

// Standing on a sign-on point, G toggles duty there before any job uses it.
jobkey.register({
    press(player) {
        const station = stationAt(player);

        if (!station) {
            return false;
        }

        if (station.onDuty(player)) {
            station.stop(player);
        } else {
            station.start(player);
        }

        station.refresh();

        return true;
    },
});

function stationOf(vehicle: Vehicle): Station | undefined {
    return stations.find((candidate) => candidate.isVehicle(vehicle));
}

// Taking the wheel, by getting in or by climbing across from another seat.
// A refused steal also puts the driver it threw out back at the wheel.
function takeWheel(vehicle: Vehicle, player: Player, stolen = false): void {
    const station = stationOf(vehicle);

    if (!station) {
        return;
    }

    if (station.onDuty(player) || station.mayDrive?.(player, vehicle)) {
        station.onDrive?.(player, vehicle);

        return;
    }

    player.removeFromVehicle();
    const last = lastDrivers.get(vehicle.id);
    const victim =
        stolen && last && Date.now() - last.at < STEAL_UNDO_MS ? playerById(last.playerId) : null;

    if (victim && victim.alive && !victim.getVehicle()) {
        victim.putInVehicle(vehicle, 0);
    }

    if (guiding.get(player.id) !== station.id) {
        station.guide(player);
    }

    toast(
        player,
        `Only ${station.crew} on duty drive this. Sign on under the arrow at the ${station.mapLabel.toLowerCase()} first: ` +
            'it is on your compass and on the city map (TAB).',
        { title: station.title, tone: 'bad', seconds: 6 },
    );
}

function leaveWheel(vehicle: Vehicle, player: Player): void {
    const station = stationOf(vehicle);

    if (!station) {
        return;
    }

    lastDrivers.set(vehicle.id, { playerId: player.id, at: Date.now() });

    if (station.onDuty(player)) {
        station.onLeave?.(player, vehicle);
    }
}

events.on('vehiclePlayerEntered', (vehicle, player, { seat, stolen }) => {
    if (seat === 0) {
        takeWheel(vehicle, player, stolen);
    }
});

events.on('vehiclePlayerSeatChanged', (vehicle, player, { seat, fromSeat }) => {
    if (fromSeat === 0) {
        leaveWheel(vehicle, player);
    }

    if (seat === 0) {
        takeWheel(vehicle, player);
    }
});

events.on('vehiclePlayerExited', (vehicle, player, { seat }) => {
    if (seat === 0) {
        leaveWheel(vehicle, player);
    }
});

// Burnt out or lost: either way a new one is sent later.
events.on('vehicleTerminal', (vehicle) => {
    for (const station of stations) {
        const slot = stateOf(station).slots.find((candidate) => candidate.vehicleId === vehicle.id);

        if (slot) {
            slot.wreckedAt ??= Date.now();
        }
    }
});

events.on('playerSpawn', (player) => {
    if (lastPoints) {
        markers(player, 'stations', JSON.parse(lastPoints) as Marker[]);
    }
});

events.on('vehicleDestroy', (vehicle) => lastDrivers.delete(vehicle.id));

// The mission's cars go with it; the new city gets its own. Nobody stands
// on a point there yet, and no compass points at the old one.
events.on('missionChange', () => {
    for (const station of stations) {
        const fleet = stateOf(station);

        for (const slot of fleet.slots) {
            retire(slot);
        }

        fleet.slots = [];
        fleet.fleetShownTo.clear();
        fleet.lastFleet = '';
    }

    for (const id of standing.keys()) {
        const player = playerById(id);

        if (player) {
            jobkey.prompt(player, null, null, 'station');
        }
    }

    for (const [id, stationId] of guiding) {
        const player = playerById(id);

        if (player) {
            compass(player, `station:${stationId}`, COMPASS_PRIORITY, null);
        }
    }

    standing.clear();
    guiding.clear();
    lastDrivers.clear();
    lastPoints = '';
});

every(TICK_MS, tick);
every(UPKEEP_MS, upkeep);
onStop(() => {
    for (const station of stations) {
        for (const slot of stateOf(station).slots) {
            retire(slot, true);
        }
    }
});
