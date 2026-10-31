// Taxi fares, the single-player way. Anyone driving a cab (taxi00,
// fordapicktaxi00, phantomtaxi00) gets hailed: a walker up the street waves
// at the cab (taxiHail) and keeps waving; stop next to them (taxiPickup) and
// they get into the back. Drive them to a corner across town on the meter,
// and arrive quickly and smoothly for a tip. A crash makes the passenger
// complain; a third gets them out without paying. Without a simulation host
// (no walkers) or when nobody hails for a while, dispatch sends the cab to a
// fare waiting at a street corner instead. /taxi starts or ends a shift and
// brings a cab.
import * as roads from './lib/roads';
import * as life from './lib/life';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { toast, card, compass, markers, COLORS, type Marker } from './lib/notify';
import {
    flatDistance,
    formatMinutes,
    formatMoney,
    onStreet,
    playable,
    plural,
    speedOf,
    vehicleById,
    type Point,
} from './lib/util';
import { events, every, onStop } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { defineShift } from './lib/shift';
import { Countdown, beacon, spawnBeside, stoppedAt } from './lib/errands';
import { reward } from './lib/rewards';

const TAXI_MODELS = new Set(['taxi00.i3d', 'fordapicktaxi00.i3d', 'phantomtaxi00.i3d']);
const HUD = 'taxi';
const COMPASS_PRIORITY = 70;
const STOP_RADIUS = 15;
const STOP_SPEED = 3;
const OUT_OF_CAB_MS = 20_000;
const TICK_MS = 500;
const CARD_MS = 1000;
const XP = 25;
// The meter: a flag drop and a rate per metre driven with the fare aboard,
// up to METER_CAP times the quote for the straight route.
const FLAG_DROP = 8;
const RATE_PER_METRE = 0.12;
const METER_CAP = 1.5;
// Tips, as shares of the quoted fare: up to SPEED_TIP for the time left of
// the ride time (retail pays the fare plus the fare times the time left),
// SMOOTH_TIP for a ride without a crash.
const SPEED_TIP = 0.4;
const SMOOTH_TIP = 0.2;
const CRASHES_TO_LEAVE = 3;
// A crash: the cab loses this much speed in one tick, or its damage changes.
const CRASH_SPEED_DROP = 9;
const CRASH_COOLDOWN_MS = 3000;
// With walkers about, dispatch calls in a corner fare only after this long
// without a hail.
const DISPATCH_AFTER_MS = 45_000;
const HAIL_TOAST_MS = 20_000;
const COMPANY_CAB = 'taxi00.i3d';

interface Ride {
    from: Point;
    target: Point;
    quote: number;
    metres: number;
    last: Point | null;
    /** Retail's ride time: what is left of it pays the speed tip. */
    tipWindow: Countdown;
    crashes: number;
    lastCrashAt: number;
    /** The total paid, once paid. */
    paid?: number;
    walkedOut?: boolean;
}

interface TaxiShift {
    phase: 'cruise' | 'boarding' | 'ride';
    fares: number;
    earned: number;
    outSince: number | null;
    cardAt: number;
    idleSince: number;
    /** Walkers waving at the cab, by pedestrian id. */
    hails: Map<number, Pedestrian>;
    hailToastAt: number;
    passenger: Npc | null;
    ride: Ride | null;
    /** A fare waiting at a street corner, sent by dispatch. */
    corner: Point | null;
    lastSpeed: number;
}

// When a player without a shift was last told someone waves.
const hailToasts = new PlayerMap<number>();
// The cab /taxi brought each player, sent back with the shift when empty. The
// old map's cabs went with it.
const companyCabs = new PlayerMap<number>({ clearOnMissionChange: true });

function cabOf(player: Player): Vehicle | null {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;

    return car && TAXI_MODELS.has(car.model.toLowerCase()) ? car : null;
}

function taxiApi(): typeof Taxi | null {
    return typeof Taxi === 'object' && Taxi ? Taxi : null;
}

function hailsAvailable(): boolean {
    return Boolean(taxiApi()?.isAvailable());
}

// Country roads have few crossings, so a fare can wait anywhere on a road.
function corner(origin: Point, minimum: number, maximum: number): Point {
    const graph = roads.current();
    const index = graph.crossingAround(origin, minimum, maximum);

    if (index !== null) {
        return graph.crossings[index];
    }

    for (let attempt = 0; attempt < 40; attempt++) {
        const { position } = graph.roadPoint();
        const gap = flatDistance(position, origin);

        if (gap >= minimum && gap <= maximum * 2) {
            return position;
        }
    }

    return graph.roadPoint().position;
}

// Retail's ride time for a path: 60 * ((20 + length / 13.8) / 60 + 1)
// seconds in whole minutes (C_traffic_element::MavaNaTaxi). The road is
// taken as a third longer than the straight line.
function rideTimeMs(straight: number): number {
    const length = straight * 1.3;

    return 60_000 * (Math.floor((20 + length * 0.072463766) / 60) + 1);
}

function quoteFor(straight: number): number {
    return Math.round(30 + straight * 0.2);
}

// The company cab goes back once nobody sits in it.
function returnCab(player: Player, delayMs = 3000): void {
    const id = companyCabs.get(player.id);

    if (id === undefined) {
        return;
    }

    companyCabs.delete(player.id);
    const send = (): void => {
        const cab = vehicleById(id);

        if (cab && cab.getOccupants().every((occupant) => occupant === null)) {
            cab.destroy();
        }
    };

    if (delayMs > 0) {
        setTimeout(send, delayMs);
    } else {
        send();
    }
}

// Walkers hail any cab driver, as in single player, but not one busy with
// something else (a race, police duty) who cannot take the fare.
every(TICK_MS, () => {
    const api = taxiApi();

    if (!api) {
        return;
    }

    for (const player of World.getPlayers()) {
        const busy = life.activity(player);
        const wanted =
            Boolean(cabOf(player)) && playable(player) && (!busy || busy.kind === 'taxi');

        if (api.isHailable(player) !== wanted) {
            api.setHailable(player, wanted);
        }
    }
});

// A stopping resource takes its timers along, so a delayed return would never
// come: the cabs go back now, before the shifts end.
onStop(() => {
    const api = taxiApi();

    for (const player of World.getPlayers()) {
        if (api?.isHailable(player)) {
            api.setHailable(player, false);
        }

        returnCab(player, 0);
    }
});

// A shift is the life activity "taxi"; a player busy with something else
// cannot take fares.
const shifts = defineShift<TaxiShift>({
    kind: 'taxi',
    activity: 'Driving a cab',
    title: 'Taxi',
    hud: HUD,
    compassPriority: COMPASS_PRIORITY,
    // Taken off the job by another module (an arrest and the jail): the fare
    // gets out.
    interrupted: 'Shift over.',
    // The old map's fare and cab are gone; nothing to tell.
    missionChange: '',
    tickMs: TICK_MS,

    begin: () => ({
        phase: 'cruise',
        fares: 0,
        earned: 0,
        outSince: null,
        cardAt: 0,
        idleSince: Date.now(),
        hails: new Map(),
        hailToastAt: 0,
        passenger: null,
        ride: null,
        corner: null,
        lastSpeed: 0,
    }),

    tick: tickShift,

    end(player, shift, how) {
        if (!how.online) {
            returnCab(player, 0);

            return;
        }

        returnCab(player);

        if (shift.passenger) {
            taxiApi()?.dropOff(shift.passenger);
        }

        markers(player, HUD, []);
    },

    summary: (shift) => `${plural(shift.fares, 'fare')}, ${formatMoney(shift.earned)}.`,
});

function clearRide(player: Player, shift: TaxiShift): void {
    shift.passenger = null;
    shift.ride = null;
    shift.phase = 'cruise';
    shift.idleSince = Date.now();
    compass(player, HUD, COMPASS_PRIORITY, null);
    showMarkers(player, shift);
}

function showMarkers(player: Player, shift: TaxiShift): void {
    const items: Marker[] = [];

    if (shift.ride?.target) {
        items.push(beacon('drop', shift.ride.target, 'DROP-OFF', COLORS.green));
    } else if (shift.corner) {
        items.push(beacon('fare', shift.corner, 'FARE WAITING', COLORS.gold));
    }

    if (!shift.ride) {
        for (const [id, pedestrian] of shift.hails) {
            items.push({
                id: `hail${id}`,
                position: pedestrian.position,
                label: 'HAILING',
                color: COLORS.gold,
                beacon: false,
                primary: false,
            });
        }
    }

    markers(player, HUD, items);
}

function start(player: Player): void {
    // A shift is on the street (lib/shift.ts): no cab for someone in an interior.
    if (!onStreet(player)) {
        return;
    }

    const busy = life.activity(player);

    if (busy && busy.kind !== 'taxi') {
        toast(player, life.busyMessage(player) ?? 'You are busy.', { tone: 'bad' });

        return;
    }

    if (!cabOf(player)) {
        const cab = spawnBeside(player, COMPANY_CAB, { owner: player, radar: 0xe2b866 });

        if (!cab) {
            toast(player, 'The cab company has nothing free.', { tone: 'bad' });

            return;
        }

        // One company cab at a time: the last one goes back.
        returnCab(player, 0);
        companyCabs.set(player.id, cab.id);
    }

    shifts.start(player);
    const shift = shifts.get(player);

    if (!shift) {
        returnCab(player);

        return;
    }

    if (hailsAvailable()) {
        toast(
            player,
            'Cruise the streets: people wave cabs down. Pull up next to one and they get in. /taxi to clock off.',
            { title: 'Taxi', tone: 'gold' },
        );
    } else {
        dispatch(player, shift);
        toast(
            player,
            'Dispatch has a fare for you. Follow the compass and stop at the corner. /taxi to clock off.',
            { title: 'Taxi', tone: 'gold' },
        );
    }
}

// The fallback: a fare waiting at a street corner.
function dispatch(player: Player, shift: TaxiShift): void {
    const cab = cabOf(player);

    shift.corner = corner(cab?.position ?? player.position, 150, 450);
    compass(player, HUD, COMPASS_PRIORITY, { position: shift.corner });
    showMarkers(player, shift);
}

function startRide(player: Player, shift: TaxiShift, from: Point, passenger: Npc | null): void {
    const target = corner(from, 700, 1600);
    const straight = flatDistance(from, target);

    shift.corner = null;
    shift.phase = 'ride';
    shift.ride = {
        from: { ...from },
        target,
        quote: quoteFor(straight),
        metres: 0,
        last: null,
        tipWindow: new Countdown(rideTimeMs(straight)),
        crashes: 0,
        lastCrashAt: 0,
    };

    if (passenger) {
        taxiApi()?.setDestination(passenger, target, STOP_RADIUS);
    }

    compass(player, HUD, COMPASS_PRIORITY, { position: target });
    showMarkers(player, shift);
    toast(
        player,
        `"Take me across town, ${Math.round(straight)} m." The meter is running; quick and smooth gets a tip.`,
        { title: 'Taxi', tone: 'gold' },
    );
}

function meterOf(ride: Ride): number {
    return Math.min(
        Math.round(FLAG_DROP + ride.metres * RATE_PER_METRE),
        Math.round(ride.quote * METER_CAP),
    );
}

function pay(player: Player, shift: TaxiShift, ride: Ride): number {
    const meter = meterOf(ride);
    const timeLeft = Math.max(0, ride.tipWindow.left()) / ride.tipWindow.ms;
    const speedTip = Math.round(ride.quote * SPEED_TIP * timeLeft);
    const smoothTip = ride.crashes === 0 ? Math.round(ride.quote * SMOOTH_TIP) : 0;
    const total = meter + speedTip + smoothTip;

    reward(player, { money: total, counters: ['fares'], xp: XP, reason: 'Taxi fare' });
    shift.fares++;
    shift.earned += total;
    const tips = [
        speedTip ? `${formatMoney(speedTip)} for speed` : null,
        smoothTip ? `${formatMoney(smoothTip)} for a smooth ride` : null,
    ].filter((tip) => tip !== null);

    toast(
        player,
        tips.length
            ? `Meter ${formatMoney(meter)}, tips ${tips.join(' and ')}.`
            : `Meter ${formatMoney(meter)}. No tip.`,
        { title: 'Taxi', tone: 'good' },
    );

    return total;
}

function crash(player: Player, shift: TaxiShift): void {
    const ride = shift.ride;
    const now = Date.now();

    // Paid or already walking out: the fare is on the pavement.
    if (!ride || ride.paid || ride.walkedOut || now - ride.lastCrashAt < CRASH_COOLDOWN_MS) {
        return;
    }

    ride.lastCrashAt = now;
    ride.crashes++;

    if (ride.crashes >= CRASHES_TO_LEAVE) {
        toast(player, '"That\'s it, let me out!" Your fare leaves without paying.', {
            title: 'Taxi',
            tone: 'bad',
        });

        ride.walkedOut = true;

        if (shift.passenger) {
            taxiApi()?.dropOff(shift.passenger);
        } else {
            clearRide(player, shift);
        }

        return;
    }

    const lines = ['"Hey! Watch the road!"', '"Are you trying to kill me?"'];

    toast(player, `${lines[Math.min(ride.crashes, lines.length) - 1]} No tip for a rough ride.`, {
        title: 'Taxi',
        tone: 'bad',
    });
}

// ---- Native taxi passengers (Taxi module) -------------------------------

events.on('taxiHail', (player, pedestrian) => {
    const shift = shifts.get(player);

    if (shift?.ride) {
        return;
    }

    if (shift) {
        shift.hails.set(pedestrian.id, pedestrian);
        showMarkers(player, shift);
    }

    const now = Date.now();
    const last = shift?.hailToastAt ?? hailToasts.get(player.id) ?? 0;

    if (now - last >= HAIL_TOAST_MS) {
        if (shift) {
            shift.hailToastAt = now;
        } else {
            hailToasts.set(player.id, now);
        }

        toast(player, 'Someone up the street is waving you down. Pull up next to them.', {
            title: 'Taxi',
            tone: 'gold',
        });
    }
});

events.on('taxiHailEnd', (player, pedestrian) => {
    const shift = shifts.get(player);

    if (shift?.hails.delete(pedestrian.id)) {
        showMarkers(player, shift);
    }
});

events.on('taxiPickup', (player, pedestrian) => {
    const api = taxiApi();

    if (!api || !cabOf(player)) {
        return;
    }

    // A cab driver without a shift starts one with the fare.
    if (!shifts.has(player)) {
        shifts.start(player);
    }

    const shift = shifts.get(player);

    if (!shift || shift.passenger || shift.ride) {
        api.decline(pedestrian);

        return;
    }

    const passenger = api.board(player, pedestrian);

    if (!passenger) {
        toast(player, 'They changed their mind.', { title: 'Taxi' });

        return;
    }

    shift.hails.clear();
    shift.corner = null;
    shift.passenger = passenger;
    shift.phase = 'boarding';
    shift.idleSince = Date.now();
    compass(player, HUD, COMPASS_PRIORITY, null);
    showMarkers(player, shift);
});

events.on('taxiPassengerEnter', (player, passenger, vehicle) => {
    const shift = shifts.get(player);

    if (shift && shift.passenger?.id === passenger.id) {
        startRide(player, shift, vehicle.position, passenger);
    }
});

events.on('taxiArrive', (player, passenger) => {
    const shift = shifts.get(player);
    const ride = shift?.ride;

    if (!shift || !ride || shift.passenger?.id !== passenger.id || ride.paid) {
        return;
    }

    ride.paid = pay(player, shift, ride);
    taxiApi()?.dropOff(passenger);
});

const EXIT_REASONS: Partial<Record<TaxiPassengerExitInfo['reason'], string>> = {
    driverLeft: 'You left the cab; your fare walked off without paying.',
    failed: 'The fare gave up on the cab.',
    died: 'Your fare is dead.',
    gone: 'Your fare is gone.',
};

events.on('taxiPassengerExit', (player, passenger, _vehicle, info) => {
    const shift = player ? shifts.get(player) : undefined;

    if (!player || !shift || !passenger || shift.passenger?.id !== passenger.id) {
        return;
    }

    const ride = shift.ride;

    if (!ride?.paid && !ride?.walkedOut) {
        const why = EXIT_REASONS[info.reason];

        if (why) {
            toast(player, why, { title: 'Taxi', tone: 'bad' });
        }
    }

    clearRide(player, shift);
});

events.on('vehicleDamage', (vehicle) => {
    for (const [player, shift] of shifts.crew()) {
        if (shift.ride && shift.phase === 'ride' && cabOf(player)?.id === vehicle.id) {
            crash(player, shift);
        }
    }
});

// ---- The shift loop ------------------------------------------------------

function tickShift(player: Player, shift: TaxiShift, now: number): void {
    // Dead: the fare gets out.
    if (!playable(player)) {
        shifts.stop(player, 'Shift over.');

        return;
    }

    const cab = cabOf(player);

    if (!cab) {
        shift.outSince ??= now;

        if (now - shift.outSince > OUT_OF_CAB_MS) {
            shifts.stop(player, 'You left the cab. Shift over.');
        }

        return;
    }

    shift.outSince = null;
    const speed = speedOf(cab);
    const ride = shift.ride;

    if (ride && shift.phase === 'ride') {
        if (ride.last) {
            ride.metres += flatDistance(ride.last, cab.position);
        }

        ride.last = { ...cab.position };

        if (shift.lastSpeed - speed >= CRASH_SPEED_DROP) {
            crash(player, shift);
        }
    }

    shift.lastSpeed = speed;

    if (!shift.passenger && !ride) {
        if (shift.corner && stoppedAt(cab, shift.corner, STOP_RADIUS, STOP_SPEED)) {
            // A corner fare rides along without a body.
            startRide(player, shift, shift.corner, null);
        } else if (
            !shift.corner &&
            (!hailsAvailable() || now - shift.idleSince >= DISPATCH_AFTER_MS) &&
            shift.hails.size === 0
        ) {
            dispatch(player, shift);
            toast(player, 'Dispatch has a fare waiting at a corner. Follow the compass.', {
                title: 'Taxi',
                tone: 'gold',
            });
        }
    } else if (
        ride &&
        !shift.passenger &&
        !ride.paid &&
        stoppedAt(cab, ride.target, STOP_RADIUS, STOP_SPEED)
    ) {
        ride.paid = pay(player, shift, ride);
        clearRide(player, shift);
        shift.idleSince = now;
    }

    if (shift.hails.size && !shift.ride) {
        showMarkers(player, shift);
    }

    if (now - shift.cardAt >= CARD_MS) {
        shift.cardAt = now;
        showCard(player, shift, cab, now);
    }
}

function showCard(player: Player, shift: TaxiShift, cab: Vehicle, now: number): void {
    const current = shift.ride;
    const rows: [string, string][] = [];
    let subtitle = 'Cruise for a fare';

    if (shift.phase === 'boarding') {
        subtitle = 'Your fare is getting in';
    } else if (current) {
        subtitle = 'Drive the fare to the drop-off';
        rows.push(
            ['Meter', formatMoney(meterOf(current))],
            ['Drop-off', `${Math.round(flatDistance(cab.position, current.target))} m`],
            [
                'Tip window',
                current.tipWindow.over(now) ? 'missed' : formatMinutes(current.tipWindow.left(now)),
            ],
            ['Ride', current.crashes ? plural(current.crashes, 'crash', 'crashes') : 'smooth'],
        );
    } else if (shift.corner) {
        subtitle = 'Pick up the fare at the corner';
        rows.push(['Distance', `${Math.round(flatDistance(cab.position, shift.corner))} m`]);
    } else if (shift.hails.size) {
        subtitle = 'Someone is waving you down';
    }

    rows.push(['This shift', `${shift.fares} · ${formatMoney(shift.earned)}`]);
    card(player, HUD, { title: 'Taxi', subtitle, rows, tone: 'gold' });
}

commands.register(
    {
        name: 'taxi',
        aliases: ['cab'],
        usage: '/taxi',
        help: 'Start or end a taxi shift.',
        category: 'City',
    },
    (player) => {
        if (shifts.has(player)) {
            shifts.stop(player, 'Shift over.');
        } else {
            start(player);
        }
    },
);

menu.provide('taxi', (player) => ({
    active: shifts.has(player),
    fares: shifts.get(player)?.fares ?? 0,
}));

export { shifts, TAXI_MODELS, rideTimeMs, quoteFor };
