// City-wide events every player sees at once: a supply drop of weapons that
// goes to whoever reaches it first, and a hot car that has to be delivered
// across town with the police on its tail. Places come from the stock road
// graph, so both always appear on a street.
import * as roads from './lib/roads';
import * as maps from './lib/maps';
import * as commands from './lib/commands';
import * as menu from './lib/menu';
import * as serverConsole from './lib/console';
import * as log from './lib/log';
import {
    toast,
    toastAll,
    card,
    compass,
    markers,
    markersAll,
    feed,
    name,
    text,
    COLORS,
    type CompassTarget,
    type Marker,
    type Tone,
    type VehicleMarker,
} from './lib/notify';
import {
    MAIN_WORLD,
    distance,
    flatDistance,
    inFreeRide,
    playable,
    formatMoney,
    formatMinutes,
    speedOf,
    vehicleById,
    pick,
    plain,
    type Point,
} from './lib/util';
import { events, every, onMissionChange, onStart, onStop } from './lib/lifecycle';
import { PlayerSet } from './lib/players';
import { reward } from './lib/rewards';
import { destroyWhenEmpty, spawnVehicle } from './lib/vehicles';

const FIRST_EVENT_MS = 3 * 60_000;
const EVENT_EVERY_MS = 9 * 60_000;
const TICK_MS = 500;
const CARD_EVERY_MS = 2000;
const DROP_LIFETIME_MS = 6 * 60_000;
const DROP_REACH = 3.5;
const DROP_PRIZE = 250;
const DROP_XP = 100;
const HOT_CAR_LIFETIME_MS = 10 * 60_000;
const HOT_CAR_PRIZE = 600;
const HOT_CAR_PASSENGER_PRIZE = 150;
const HOT_CAR_XP = 150;
const DELIVERY_RADIUS = 14;
// An abandoned contract car leaves once nobody sits in it, this long after the event.
const HOT_CAR_LINGER_MS = 60_000;
// The card is for players at the scene; others see the toast and the map
// blip, and /event points their compass at it.
const CARD_RANGE = 250;

const HOT_CARS = [
    { model: 'bugatti00.i3d', name: 'Bugatti Type 57' },
    { model: 'duesenberg00.i3d', name: 'Duesenberg' },
    { model: 'alfa8c00.i3d', name: 'Alfa Romeo 8C' },
    { model: 'speedster00.i3d', name: 'Speedster' },
    { model: 'merced500k00.i3d', name: 'Mercedes 500K' },
];
const DROP_LOOT = [
    { weaponId: 10, loaded: 50, reserve: 100 },
    { weaponId: 11, loaded: 8, reserve: 24 },
    { weaponId: 15, loaded: 3, reserve: 0 },
    { weaponId: 6, loaded: 6, reserve: 30 },
];

type EventKind = 'supply' | 'hotcar';

interface EventBase {
    title: string;
    expiresAt: number;
    generation: number;
}

interface SupplyDrop extends EventBase {
    kind: 'supply';
    position: Point;
}

interface HotCar extends EventBase {
    kind: 'hotcar';
    vehicleId: number;
    carName: string;
    dropOff: string;
    delivery: Point;
    driverId: number | null;
    /** Drivers already reported to the police. */
    wanted: Set<number>;
}

type CityEvent = SupplyDrop | HotCar;

let current: CityEvent | null = null;
let nextEventAt = 0;
let lastCardAt = 0;
// Players who asked /event to follow the current event.
const following = new PlayerSet({ clearOnMissionChange: true });

function streetPlayers(): Player[] {
    return World.getPlayers().filter(
        (player) => playable(player) && player.virtualWorld === MAIN_WORLD,
    );
}

function spotNear(minimum: number, maximum: number): roads.RoadSpot | null {
    const players = streetPlayers();

    if (!players.length) {
        return null;
    }

    const anchor = pick(players);
    const graph = roads.current();
    const index =
        graph.crossingAround(anchor.position, minimum, maximum) ??
        graph.crossingAround(anchor.position, 0, maximum * 2);

    return index === null ? graph.roadPoint() : graph.roadside(index);
}

function eventMarkers(event: CityEvent): (Marker | VehicleMarker)[] {
    if (event.kind === 'supply') {
        return [
            {
                id: 'drop',
                position: event.position,
                label: 'SUPPLY DROP',
                color: COLORS.gold,
                beacon: true,
                model: 'bedna00.i3d',
                primary: true,
            },
        ];
    }

    return [
        {
            id: 'car',
            vehicleId: event.vehicleId,
            label: `HOT CAR · ${event.carName.toUpperCase()}`,
            color: COLORS.red,
            beacon: true,
            primary: true,
        },
        {
            id: 'delivery',
            position: event.delivery,
            label: `DELIVER TO ${event.dropOff.toUpperCase()}`,
            color: COLORS.green,
            beacon: true,
        },
    ];
}

function clearPresentation(): void {
    markersAll('event', []);

    for (const player of World.getPlayers()) {
        compass(player, 'event', 40, null);
        card(player, 'event', null);
    }

    following.clear();
}

function endEvent(message: string | null, tone: Tone = 'info'): void {
    if (!current) {
        return;
    }

    const ended = current;

    current = null;
    nextEventAt = Date.now() + EVENT_EVERY_MS;
    clearPresentation();

    if (ended.kind === 'hotcar') {
        const vehicle = vehicleById(ended.vehicleId);

        if (vehicle) {
            vehicle.setRadarMarker(false);
            vehicle.state.remove('fr.hot');
            destroyWhenEmpty(ended.vehicleId, HOT_CAR_LINGER_MS);
        }
    }

    if (message) {
        toastAll(message, { title: ended.title, tone });
    }
}

function startSupplyDrop(): boolean {
    const spot = spotNear(300, 900);

    if (!spot) {
        return false;
    }

    const position = { x: spot.position.x, y: spot.position.y + 0.4, z: spot.position.z };
    const drop: SupplyDrop = {
        kind: 'supply',
        title: 'Supply drop',
        position,
        expiresAt: Date.now() + DROP_LIFETIME_MS,
        generation: World.getMissionGeneration(),
    };

    current = drop;
    markersAll('event', eventMarkers(drop));
    toastAll(
        'A crate of guns fell off a truck. First one to it keeps the lot. ' +
            'It is on the city map (TAB); /event points your compass at it.',
        { title: 'Supply drop', tone: 'gold', seconds: 8 },
    );

    feed('event', [text('Supply drop', COLORS.gold), text(' landed somewhere in the city')]);

    return true;
}

function startHotCar(): boolean {
    const spot = spotNear(400, 1000);

    if (!spot) {
        return false;
    }

    const choice = pick(HOT_CARS);
    const dropOffs = maps.places();
    const far = dropOffs.filter((entry) => flatDistance(entry.position, spot.position) > 800);
    const dropOff = far.length ? pick(far) : pick(dropOffs);
    const graph = roads.current();
    const delivery = graph.crossings[graph.nearestCrossing(dropOff.position)];
    const heading = Math.atan2(spot.direction.x, spot.direction.z);
    const vehicle = spawnVehicle(
        choice.model,
        { x: spot.position.x, y: spot.position.y + 0.6, z: spot.position.z },
        heading,
        { radar: 0xd0574a, state: { 'fr.hot': true } },
    );

    if (!vehicle) {
        return false;
    }

    const hotCar: HotCar = {
        kind: 'hotcar',
        title: 'Hot car',
        vehicleId: vehicle.id,
        carName: choice.name,
        dropOff: dropOff.name,
        delivery: plain(delivery),
        driverId: null,
        wanted: new Set(),
        expiresAt: Date.now() + HOT_CAR_LIFETIME_MS,
        generation: World.getMissionGeneration(),
    };

    current = hotCar;
    markersAll('event', eventMarkers(hotCar));
    toastAll(
        `A ${choice.name} needs to reach ${dropOff.name}, no questions asked. ` +
            `${formatMoney(HOT_CAR_PRIZE)} for the driver. ` +
            'It is on the city map (TAB); /event points your compass at it.',
        { title: 'Hot car', tone: 'gold', seconds: 9 },
    );

    feed('event', [
        text('Hot car', COLORS.red),
        text(` · a ${choice.name} is waiting for a driver`),
    ]);

    return true;
}

function startEvent(kind?: EventKind): boolean {
    if (current || !inFreeRide() || !World.isReady() || !streetPlayers().length) {
        return false;
    }

    const started =
        (kind ?? pick<EventKind>(['supply', 'hotcar'])) === 'supply'
            ? startSupplyDrop()
            : startHotCar();

    nextEventAt = Date.now() + EVENT_EVERY_MS;

    return started;
}

function tickSupply(drop: SupplyDrop, now: number): void {
    for (const player of streetPlayers()) {
        if (distance(player.position, drop.position) > DROP_REACH) {
            continue;
        }

        const { position } = drop;

        DROP_LOOT.forEach((item, index) => {
            const angle = (index / DROP_LOOT.length) * Math.PI * 2;

            Pickup.create(
                item.weaponId,
                {
                    x: position.x + Math.cos(angle) * 1.4,
                    y: position.y,
                    z: position.z + Math.sin(angle) * 1.4,
                },
                angle,
                item.loaded,
                item.reserve,
                120_000,
            );
        });

        reward(player, {
            money: DROP_PRIZE,
            counters: ['eventsWon'],
            xp: DROP_XP,
            reason: 'Supply drop',
        });

        feed('event', [
            name(player, COLORS.gold),
            text(' cracked the supply drop · '),
            text(formatMoney(DROP_PRIZE), COLORS.gold),
        ]);

        toast(
            player,
            `The crate is yours, plus ${formatMoney(DROP_PRIZE)} under the straw. Grab the guns.`,
            { title: 'Supply drop', tone: 'good' },
        );

        endEvent(null);

        return;
    }

    if (now >= drop.expiresAt) {
        endEvent("Someone else got to the supply drop first. It's gone.");
    }
}

function tickHotCar(hotCar: HotCar, now: number): void {
    const vehicle = vehicleById(hotCar.vehicleId);

    if (!vehicle || vehicle.terminalState !== 0) {
        endEvent('The hot car was wrecked. The buyer walked away.', 'bad');

        return;
    }

    const driver = vehicle.getDriver();

    if ((driver?.id ?? null) !== hotCar.driverId) {
        hotCar.driverId = driver?.id ?? null;

        for (const player of World.getPlayers()) {
            const isDriver = driver && player.id === driver.id;

            if (isDriver || following.has(player.id)) {
                compass(
                    player,
                    'event',
                    40,
                    isDriver ? { position: hotCar.delivery } : { vehicleId: hotCar.vehicleId },
                );
            } else {
                compass(player, 'event', 40, null);
            }
        }

        if (driver) {
            feed('event', [name(driver, COLORS.red), text(` took the ${hotCar.carName}`)]);
            toast(driver, `Get it to ${hotCar.dropOff} in one piece. The police already know.`, {
                title: 'Hot car',
                tone: 'bad',
            });

            // The simulation host runs the police; without it there is no chase.
            if (!hotCar.wanted.has(driver.id) && Police.setWanted(driver, 2)) {
                hotCar.wanted.add(driver.id);
            }
        }
    }

    if (
        driver &&
        flatDistance(vehicle.position, hotCar.delivery) <= DELIVERY_RADIUS &&
        speedOf(vehicle) < 4
    ) {
        reward(driver, {
            money: HOT_CAR_PRIZE,
            counters: ['eventsWon'],
            xp: HOT_CAR_XP,
            reason: 'Hot car delivered',
        });

        for (const passenger of vehicle.getOccupants()) {
            if (!passenger || passenger.id === driver.id) {
                continue;
            }

            reward(passenger, {
                money: HOT_CAR_PASSENGER_PRIZE,
                xp: HOT_CAR_XP / 3,
                reason: 'Rode along',
            });
        }

        feed('event', [
            name(driver, COLORS.gold),
            text(` delivered the ${hotCar.carName} · `),
            text(formatMoney(HOT_CAR_PRIZE), COLORS.gold),
        ]);

        endEvent(null);

        return;
    }

    if (now >= hotCar.expiresAt) {
        endEvent('The buyer stopped waiting for the hot car.');
    }
}

function cardSubtitle(event: CityEvent, driving: boolean): string {
    if (event.kind === 'supply') {
        return 'First to the crate keeps it';
    }

    if (driving) {
        return `Deliver to ${event.dropOff}`;
    }

    return event.driverId
        ? 'Someone has the car. Stop them or ride along'
        : 'Find the car and take the wheel';
}

function sendCards(event: CityEvent, now: number): void {
    if (now - lastCardAt < CARD_EVERY_MS) {
        return;
    }

    lastCardAt = now;
    const vehicle = event.kind === 'hotcar' ? vehicleById(event.vehicleId) : null;
    const target = event.kind === 'supply' ? event.position : vehicle?.position;

    for (const player of World.getPlayers()) {
        if (!player.spawned) {
            continue;
        }

        const driving = Boolean(vehicle && vehicle.getDriver()?.id === player.id);
        const aboard = vehicle?.getOccupants().some((occupant) => occupant?.id === player.id);
        const goal = driving && event.kind === 'hotcar' ? event.delivery : target;

        // Away from the scene the announcement and the map blip are enough,
        // unless the player follows the event. (A hot car's goal is there:
        // tickHotCar ends the event when the car is gone.)
        if (
            !goal ||
            (!aboard && !following.has(player.id) && distance(player.position, goal) > CARD_RANGE)
        ) {
            card(player, 'event', null);
            continue;
        }

        const supply = event.kind === 'supply';

        card(player, 'event', {
            title: event.title,
            subtitle: cardSubtitle(event, driving),
            rows: [
                ['Distance', `${Math.round(distance(player.position, goal) / 10) * 10} m`],
                ['Ends in', formatMinutes(event.expiresAt - now)],
                ['Prize', formatMoney(supply ? DROP_PRIZE : HOT_CAR_PRIZE)],
            ],
            progress: Math.max(
                0,
                (event.expiresAt - now) / (supply ? DROP_LIFETIME_MS : HOT_CAR_LIFETIME_MS),
            ),
            tone: supply ? 'gold' : 'bad',
        });
    }
}

function tick(): void {
    const now = Date.now();

    if (!current) {
        if (nextEventAt && now >= nextEventAt) {
            startEvent();
        }

        return;
    }

    if (current.generation !== World.getMissionGeneration()) {
        current = null;

        return;
    }

    if (current.kind === 'supply') {
        tickSupply(current, now);
    } else {
        tickHotCar(current, now);
    }

    if (current) {
        sendCards(current, now);
    }
}

function followTarget(event: CityEvent): CompassTarget | null {
    if (event.kind === 'supply') {
        return { position: event.position };
    }

    return vehicleById(event.vehicleId) ? { vehicleId: event.vehicleId } : null;
}

// Late arrivals see the event already in progress.
function catchUp(player: Player): void {
    if (!current) {
        return;
    }

    markers(player, 'event', eventMarkers(current));

    if (following.has(player.id)) {
        compass(player, 'event', 40, followTarget(current));
    }
}

onStart(() => {
    nextEventAt = Date.now() + FIRST_EVENT_MS;
});

every(TICK_MS, tick);

onStop(() => endEvent(null));

onMissionChange(() => {
    current = null;
    nextEventAt = Date.now() + FIRST_EVENT_MS;
});

events.on('playerSpawn', catchUp);
events.on('vehicleDestroy', (vehicle) => {
    if (current?.kind === 'hotcar' && current.vehicleId === vehicle.id) {
        endEvent('The hot car is gone.', 'bad');
    }
});

serverConsole.action('fr', 'event', ([kind]) => {
    if (current) {
        endEvent('City Hall called off the event.');
    }

    const started = startEvent(kind === 'hotcar' || kind === 'supply' ? kind : undefined);

    log.info(
        started && current
            ? `Started ${current.kind}`
            : 'No event could start (needs a player on the street)',
    );
});

// Follows the current event (compass and card wherever the player is), or
// stops following it.
commands.register(
    {
        name: 'event',
        usage: '/event',
        help: 'Follow the current city event, or stop following it.',
        category: 'City',
    },
    (player) => {
        if (!current) {
            toast(
                player,
                `No event right now. The next one comes in about ${formatMinutes(Math.max(0, nextEventAt - Date.now()))}.`,
            );

            return;
        }

        if (following.delete(player.id)) {
            compass(player, 'event', 40, null);
            card(player, 'event', null);
            toast(player, 'You stopped following the event.', {
                title: current.title,
                seconds: 3,
            });

            return;
        }

        following.add(player.id);
        catchUp(player);
        toast(
            player,
            current.kind === 'supply'
                ? 'A supply drop is out there. Your compass points at it. /event again to stop.'
                : `A hot ${current.carName} is waiting. Your compass points at it. /event again to stop.`,
            { title: current.title, tone: 'gold' },
        );
    },
);

menu.provide('event', (player) =>
    current
        ? {
              kind: current.kind,
              title: current.title,
              following: following.has(player.id),
              detail:
                  current.kind === 'supply'
                      ? 'First to the crate keeps it'
                      : `${current.carName} to ${current.dropOff}`,
              secondsLeft: Math.max(0, Math.ceil((current.expiresAt - Date.now()) / 1000)),
          }
        : {
              kind: null,
              nextInSeconds: Math.max(0, Math.ceil((nextEventAt - Date.now()) / 1000)),
          },
);
