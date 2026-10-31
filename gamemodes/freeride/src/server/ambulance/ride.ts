// The ride in: the badly hurt get into the ambulance and ride to the
// hospital, quickly for a tip; a woman in labour cries out at every bump.
import { CIVILIANS, PAIN_LINES, say } from '../lib/civilians';
import { npcsAvailable, walkOff } from '../lib/npcs';
import { COLORS, feed, name, text, toast } from '../lib/notify';
import { events } from '../lib/lifecycle';
import {
    distance,
    flatDistance,
    formatMinutes,
    formatMoney,
    pick,
    plain,
    speedOf,
    type Point,
} from '../lib/util';
import { callOf, calls, leaveGroup, presentCalls, stretcher, type Call, type Ride } from './board';
import { ambulanceOf, medics, type Medic } from './duty';
import { PAY, PER_METRE, RIDE_TIP, SMOOTH_TIP, payMedic } from './pay';
import { hospital, type HospitalPlace } from './places';

const PICKUP_REACH = 20;
const CRASH_COOLDOWN_MS = 3000;

// The exit tasks of patients at the hospital, after which they walk in.
const arrivals = new Map<number, { npc: Npc; door: Point }>();

/** Badly hurt: to the hospital, in the ambulance. */
export function toHospital(player: Player, medic: Medic, call: Call, opening: string): void {
    const ambulance = ambulanceOf(medic);
    const home = hospital();

    if (!ambulance || !home) {
        toast(
            player,
            'You need an ambulance to take them in: there are two in front of the hospital.',
            { tone: 'bad' },
        );

        return;
    }

    // They get in once the medic is back at the wheel: a passenger of a car
    // a player drives rides along (NpcService's player-driven seat rule),
    // while an empty car would go to the simulation host.
    call.state = call.npc ? 'pickup' : 'aboard';
    medic.patient = call.id;
    const straight = flatDistance(call.position, home.bay);
    const deadline = Date.now() + (straight / 11 + (call.kind === 'labour' ? 60 : 90)) * 1000;

    call.ride = { deadline, from: plain(call.position), crashes: 0, joltAt: 0 };
    medic.lastSpeed = null;
    presentCalls();
    toast(
        player,
        `${opening}Bring the ambulance up and stop beside them, then get them to ${home.name}; ` +
            `${formatMinutes(deadline - Date.now())} for a tip${call.kind === 'labour' ? ', and drive smoothly' : ''}.`,
        { title: 'Ambulance', tone: 'gold' },
    );
}

/** A bump on the way in with a woman in labour aboard. */
export function jolt(player: Player, call: Call): void {
    const ride = call.ride;
    const now = Date.now();

    if (!ride || now - ride.joltAt < CRASH_COOLDOWN_MS) {
        return;
    }

    ride.joltAt = now;
    ride.crashes++;

    if (call.npc) {
        say(call.npc, PAIN_LINES);
    }

    toast(
        player,
        `${pick(['"Easy! The baby!"', '"Are you trying to deliver it here?"', '"Mind the road!"'])} No smooth-ride tip now.`,
        { title: 'Ambulance', tone: 'bad', seconds: 4 },
    );
}

/** The patient boards a still ambulance its medic drives, in a free back or passenger seat. */
export function boardAmbulance(player: Player, medic: Medic, call: Call): void {
    const ambulance = ambulanceOf(medic);
    const npc = call.npc;

    if (!npc) {
        stretcher(call);

        return;
    }

    if (
        !ambulance ||
        ambulance.getDriver()?.id !== player.id ||
        speedOf(ambulance) > 1 ||
        distance(ambulance.position, npc.position) > PICKUP_REACH
    ) {
        return;
    }

    const occupants = ambulance.getOccupants();
    const seat = [1, 2, 3].find((index) => index < ambulance.seatCount && !occupants[index]) ?? 1;

    call.task = npc.enterVehicle(ambulance, seat);

    if (!call.task) {
        stretcher(call);

        return;
    }

    call.state = 'boarding';
    call.boardSince = Date.now();
}

export function deliver(
    player: Player,
    medic: Medic,
    call: Call,
    ride: Ride,
    home: HospitalPlace,
): void {
    const ambulance = ambulanceOf(medic);

    medic.patient = null;
    calls.delete(call.id);
    call.resolved = true;
    leaveGroup(call);
    const inTime = Date.now() <= ride.deadline;
    let base: number = PAY.serious;

    if (call.kind === 'labour') {
        base = PAY.labour;
    } else if (call.kind === 'cardiac') {
        base = PAY.cardiac;
    } else if (call.triage === 'critical') {
        base = PAY.critical;
    }

    const fare = base + Math.round(flatDistance(ride.from, home.bay) * PER_METRE);
    const tip = inTime ? Math.round(fare * RIDE_TIP) : 0;
    const smooth = call.kind === 'labour' && !ride.crashes ? Math.round(fare * SMOOTH_TIP) : 0;
    const bonus = call.bonus;

    payMedic(player, medic, fare + tip + smooth + bonus, 'Brought a patient in');
    const extras = [
        tip ? `a ${formatMoney(tip)} tip` : '',
        smooth ? `${formatMoney(smooth)} for the smooth ride` : '',
        bonus ? `${formatMoney(bonus)} from the fire call` : '',
    ].filter(Boolean);
    const baby = pick(['boy', 'girl']);

    toast(
        player,
        `${call.kind === 'labour' ? `It's a ${baby}! ` : ''}${inTime ? 'Delivered in time' : 'Delivered, but slowly'}. ${formatMoney(fare)}` +
            `${extras.length ? ` and ${extras.join(' and ')}` : ''}.`,
        { title: 'Ambulance', tone: 'good', seconds: 6 },
    );

    feed('medic', [
        name(player, COLORS.green),
        text(
            call.kind === 'labour'
                ? ` rushed a mother to ${home.name}: it's a ${baby}`
                : ` brought a patient to ${home.name}`,
        ),
    ]);

    walkIn(call.npc, ambulance, home);
    presentCalls();
}

// The patient walks in through the doors, once out of the car (a walk given
// earlier would cancel the getting out). One carried in on the stretcher
// gets out of the back on their own feet.
function walkIn(patient: Npc | null, ambulance: Vehicle | null, home: HospitalPlace): void {
    const forget = (npc: Npc) => (): void => {
        for (const [task, arrival] of arrivals) {
            if (arrival.npc === npc) {
                arrivals.delete(task);
            }
        }
    };

    if (patient) {
        const exit = patient.exitVehicle();

        if (exit) {
            arrivals.set(exit, { npc: patient, door: home.door });
        }

        walkOff(patient, 25_000, { to: exit ? null : home.door, gone: forget(patient) });

        return;
    }

    if (!npcsAvailable() || !ambulance) {
        return;
    }

    const { x, y, z } = ambulance.position;
    const npc = Npc.spawn(pick(CIVILIANS), { x: x + 1.5, y: y + 0.2, z: z + 1.5 }, 0, {
        behaviour: 'neutral',
    });

    if (npc) {
        setTimeout(() => npc.walkTo(home.door), 600);
        walkOff(npc, 25_000, { gone: forget(npc) });
    }
}

/** The mission change removed every patient. */
export function forgetArrivals(): void {
    arrivals.clear();
}

events.on('npcTaskComplete', (npc, info) => {
    const arrival = arrivals.get(info.id);

    if (arrival && arrival.npc.id === npc.id) {
        arrivals.delete(info.id);
        npc.walkTo(arrival.door);

        return;
    }

    for (const call of calls.values()) {
        if (call.npc?.id !== npc.id || call.state !== 'boarding' || info.id !== call.task) {
            continue;
        }

        if (info.success) {
            call.state = 'aboard';
        } else {
            stretcher(call);
        }
    }
});

// Bumps with a woman in labour aboard.
events.on('vehicleDamage', (vehicle) => {
    for (const [player, medic] of medics.crew()) {
        const call = callOf(medic.patient);

        if (
            call?.kind !== 'labour' ||
            (call.state !== 'aboard' && call.state !== 'boarding') ||
            ambulanceOf(medic)?.id !== vehicle.id
        ) {
            continue;
        }

        if (player.getVehicle()?.id === vehicle.id) {
            jolt(player, call);
        }
    }
});
