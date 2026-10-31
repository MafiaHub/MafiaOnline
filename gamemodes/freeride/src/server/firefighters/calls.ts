// Where fires break out and how a call ends. // Fires break out across the
// city on the road graph: a house front, a burning car, a crash with people
// trapped in it; with a crew on duty the bigger calls too (pumps.ts,
// bar.ts, building.ts), and a pile-up the ambulance answers may have a wreck
// burning.
// The pay for a fire is split by how much water each firefighter put on it.
import * as bars from '../bars';
import * as profiles from '../lib/profiles';
import { CallClock, names, roadside, spawnWreck } from '../lib/callouts';
import { CROUCHED, LYING } from '../lib/civilians';
import { npcsAvailable } from '../lib/npcs';
import { COLORS, feed, text, toast, toastAll } from '../lib/notify';
import { events } from '../lib/lifecycle';
import {
    formatMoney,
    inFreeRide,
    onStreet,
    pick,
    playerById,
    vehicleById,
    type Point,
} from '../lib/util';
import { occupied } from '../lib/vehicles';
import { ARSONIST_CHANCE, dismissArsonist, spawnArsonist, updateArsonist } from './arson';
import { startBarFire } from './bar';
import {
    evacuate,
    forgetAll as forgetBuildings,
    startBuildingFire,
    updateBuilding,
} from './building';
import { available, crews, earn, station } from './duty';
import { presentIncidents } from './markers';
import {
    burning,
    firesOf,
    fleeing,
    incidentOfFire,
    incidents,
    newIncident,
    titleOf,
    waiting,
    type Incident,
    type IncidentKind,
} from './model';
import { startPumpFire, updatePumps } from './pumps';
import { trap, updateCivilians } from './rescue';

const FIRST_FIRE_MS = 90_000;
const FIRE_EVERY_MS = 4 * 60_000;
const QUIET_FIRE_EVERY_MS = 11 * 60_000;
const MAX_INCIDENTS = 2;
const INCIDENT_LIFETIME_MS = 9 * 60_000;
const CAR_BLOWS_AFTER_MS = 4 * 60_000;
const SMOKE_MS = 25_000;
// The big calls come to about $500-900 for one firefighter: the filling
// station $380 + $40 + $160 = $580 saved; the bar $260 + 2 x $40 + 3 x $50
// + $160 = $650 with everyone out; a burning flat $300 + 3-5 x $40 + 1-2 x
// $120 + $200 = $860 or so, a villa somewhat more.
const PAY: Readonly<Record<IncidentKind, number>> = {
    house: 420,
    car: 260,
    crash: 320,
    pump: 380,
    bar: 260,
    pileup: 260,
    building: 300,
};
// Each further fire of a call (the pumps, the rooms of a bar).
const SPOT_PAY = 40;
// For the whole crew, split half by water, half evenly: the pumps saved,
// everyone out of the bar.
const BONUS: Partial<Record<IncidentKind, number>> = { pump: 160, bar: 160, building: 200 };
const XP = 70;

export const clock = new CallClock(FIRST_FIRE_MS);
// The kind of the last call, which the next one is not.
let lastKind: IncidentKind | null = null;

/** A pile-up's wreck already on the road (ambulance/scenes.ts), left standing when the call ends. */
export interface PileupWreck {
    position: Point;
    vehicleId: number;
}

export function startIncident(
    kind: IncidentKind,
    origin: Point | null,
    wreck: PileupWreck | null = null,
): Incident | null {
    if (!available() || !inFreeRide() || !World.isReady() || incidents.size >= MAX_INCIDENTS) {
        return null;
    }

    switch (kind) {
        case 'pump':
            return startPumpFire(origin);
        case 'bar':
            return startBarFire();
        case 'building':
            return startBuildingFire(origin);
        case 'pileup':
            return wreck ? startPileupFire(wreck) : null;
        default:
            return startStreetFire(kind, origin);
    }
}

// A house front, a burning car or a crash, beside a road.
function startStreetFire(kind: 'house' | 'car' | 'crash', origin: Point | null): Incident | null {
    const crash = kind === 'crash';
    const house = kind === 'house';
    const spot = roadside(origin, 250, 1100, house ? 9 : 3.6);
    let vehicleId: number | null = null;

    if (!house) {
        const wreck = spawnWreck(
            spot.position,
            spot.heading + (crash ? (Math.random() - 0.5) * 1.2 : 0),
        );

        if (!wreck) {
            return null;
        }

        vehicleId = wreck.id;
    }

    const fire = Fire.create(
        { x: spot.position.x, y: spot.position.y + (house ? 0 : 0.4), z: spot.position.z },
        house
            ? { radius: 4.5, height: 5, toughness: 4, growth: 0.012, damage: 18 }
            : {
                  radius: 2.6,
                  height: 1,
                  toughness: 2,
                  growth: 0.01,
                  damage: 15,
                  attachTo: vehicleById(vehicleId) ?? undefined,
              },
    );

    if (!fire) {
        vehicleById(vehicleId)?.destroy();

        return null;
    }

    const incident = newIncident(
        kind,
        spot.position,
        house ? 'HOUSE FIRE' : crash ? 'CRASH · PEOPLE TRAPPED' : 'CAR FIRE',
        fire,
        { vehicleId },
    );

    incidents.set(incident.id, incident);

    // Who is caught in it: the crash's passengers on the road, a resident
    // crouched at the house door. Only with a crew on duty to get them out.
    if (crews.size && crash) {
        const people = 1 + Math.floor(Math.random() * 2);

        for (let index = 0; index < people; index++) {
            const angle = spot.heading + Math.PI / 2 + (index ? Math.PI : 0);

            trap(
                incident,
                {
                    x: spot.position.x + Math.sin(angle) * 2.2,
                    y: spot.position.y + 0.2,
                    z: spot.position.z + Math.cos(angle) * 2.2,
                },
                LYING,
                index,
            );
        }
    } else if (crews.size && house && Math.random() < 0.6) {
        trap(
            incident,
            { x: spot.position.x, y: spot.position.y + 0.2, z: spot.position.z },
            CROUCHED,
            0,
        );
    }

    if (crews.size && house && npcsAvailable() && Math.random() < ARSONIST_CHANCE) {
        spawnArsonist(incident, spot);
    }

    if (incident.arsonist) {
        incident.label = 'HOUSE FIRE · ARSON';
        toastAll(
            `Someone set a house alight ${incident.where}, and he is running from it. Put the fire out; firefighters and police on duty ` +
                'can grab him: G next to him.',
            { title: 'Arson', tone: 'bad', seconds: 9 },
        );
    } else {
        const what = house
            ? 'A house is on fire'
            : crash
              ? 'A car crashed and caught fire with people inside'
              : 'A car is burning';

        toastAll(
            `${what} ${incident.where}.` +
                (crews.size ? '' : ' Nobody is on duty at the fire station.'),
            { title: 'Fire', tone: 'bad', seconds: 7 },
        );
    }

    feed('fire', [
        text('Fire', COLORS.red),
        text(` · ${incident.label.toLowerCase()} ${incident.where}`),
    ]);

    presentIncidents();

    return incident;
}

// A burning wreck of the ambulance's pile-up.
function startPileupFire({ position, vehicleId }: PileupWreck): Incident | null {
    const wreck = vehicleById(vehicleId);

    if (!wreck) {
        return null;
    }

    const fire = Fire.create(
        { x: position.x, y: position.y + 0.4, z: position.z },
        { radius: 2.6, height: 1, toughness: 2, growth: 0.01, damage: 15, attachTo: wreck },
    );

    if (!fire) {
        return null;
    }

    const incident = newIncident('pileup', { ...position }, 'PILE-UP · WRECK ON FIRE', fire, {
        vehicleId,
        keepVehicle: true,
    });

    incidents.set(incident.id, incident);

    for (const [player] of crews.crew()) {
        toast(
            player,
            `A pile-up ${incident.where}: a wreck is burning beside the injured. Put it out before it reaches them.`,
            { title: 'Fire', tone: 'bad', seconds: 7 },
        );
    }

    presentIncidents();

    return incident;
}

function endIncident(incident: Incident, message: string | null, smokeMs = SMOKE_MS): void {
    incidents.delete(incident.id);

    for (const civilian of incident.civilians) {
        civilian.npc?.remove();
        civilian.outside?.remove();
    }

    dismissArsonist(incident);
    // Nobody can open a finished call's door from the inside: out they come.
    evacuate(incident, smokeMs === 0);

    if (incident.bar) {
        bars.setClosed(incident.bar.id, false);
    }

    const fires = firesOf(incident);
    // A pile-up's wrecks are the ambulance's to clear.
    const wreckId = incident.keepVehicle ? null : incident.vehicleId;
    // The smoke stays a while, then the wreck is towed away with it.
    const clear = (): void => {
        for (const fire of fires) {
            fire.destroy();
        }

        const wreck = vehicleById(wreckId);

        if (wreck && !occupied(wreck)) {
            wreck.destroy();
        }
    };

    if (smokeMs > 0) {
        setTimeout(clear, smokeMs);
    } else {
        clear();
    }

    if (message) {
        feed('fire', [text('Fire', COLORS.muted), text(` · ${message}`)]);
    }

    presentIncidents();
}

// The call is over with everyone out: the big ones pay the crew a bonus.
function complete(incident: Incident): void {
    const bonus = BONUS[incident.kind];

    if (!bonus) {
        return;
    }

    if (incident.kind === 'pump' && incident.blown) {
        return;
    }

    if (
        (incident.bar || incident.building) &&
        incident.civilians.some((civilian) => civilian.state !== 'safe')
    ) {
        return;
    }

    const total = [...incident.water.values()].reduce((sum, amount) => sum + amount, 0);
    const shares = new Map(
        [...incident.water].map(([id, amount]) => [id, total > 0 ? amount / total : 0]),
    );

    // Everyone who led someone out of the bar has a part in it too.
    for (const id of incident.rescuers) {
        if (!shares.has(id)) {
            shares.set(id, 0);
        }
    }

    const people = [...shares].flatMap(([id, share]) => {
        const player = playerById(id);

        return player ? [{ player, share }] : [];
    });

    if (!people.length) {
        return;
    }

    const even = 1 / people.length;

    for (const { player, share } of people) {
        // Half by water, half evenly: the one who went in counts as much.
        const cut = Math.max(25, Math.round((bonus * (share + even)) / 2));

        player.giveMoney(cut);
        earn(player, cut);
        profiles.addXp(
            player,
            40,
            incident.kind === 'pump' ? 'Saved the pumps' : 'Got everyone out',
        );

        toast(
            player,
            `${incident.kind === 'pump' ? 'The pumps are saved' : 'Everyone got out alive'}. Bonus: ${formatMoney(cut)}.`,
            { title: 'Fire brigade', tone: 'good' },
        );
    }

    feed('fire', [
        ...names(
            people.map(({ player }) => player),
            COLORS.red,
        ),
        text(
            incident.kind === 'pump'
                ? ` saved the filling station ${incident.where}`
                : ` got everyone out of ${incident.bar?.name ?? incident.building?.name}`,
        ),
    ]);
}

/** A new fire now and then; more often with the brigade on duty. Only on maps with a fire station to call. */
export function dispatch(now: number): void {
    const street = World.getPlayers().filter(onStreet);

    if (
        !available() ||
        !station.here() ||
        !street.length ||
        !clock.due(now, crews.size ? FIRE_EVERY_MS : QUIET_FIRE_EVERY_MS)
    ) {
        return;
    }

    // A crew on duty also gets the bigger calls: the filling station, the
    // bar and a building with people inside, the one they get most. The same
    // kind of call does not come twice running while another would do.
    const anchor: Player | undefined = pick(
        crews.size ? crews.crew().map(([player]) => player) : street,
    );
    const kinds: IncidentKind[] = crews.size
        ? ['building', 'building', 'house', 'car', 'crash', 'pump', 'bar']
        : ['house', 'house', 'car', 'crash'];
    const fresh = kinds.filter((entry) => entry !== lastKind);
    const kind = pick(fresh.length ? fresh : kinds);
    const origin = anchor?.position ?? null;
    const started =
        startIncident(kind, origin) ??
        (kind === 'pump' || kind === 'bar' || kind === 'building'
            ? startIncident('house', origin)
            : null);

    if (started) {
        lastKind = started.kind;
    }
}

/** Each tick: the people, the arsonist, the pumps; a call out, burnt out or blown up. */
export function updateIncidents(now: number): void {
    for (const incident of [...incidents.values()]) {
        if (incident.generation !== World.getMissionGeneration()) {
            incidents.delete(incident.id);
            continue;
        }

        updateCivilians(incident, now);
        updateArsonist(incident, now);
        updateBuilding(incident, now);

        if (!burning(incident) && !waiting(incident) && !fleeing(incident)) {
            complete(incident);
            endIncident(incident, null);
            continue;
        }

        updatePumps(incident, now);
        const age = now - incident.startedAt;

        if (
            incident.vehicleId &&
            incident.fire.burning &&
            age >= CAR_BLOWS_AFTER_MS &&
            !incident.blown &&
            incident.kind !== 'pump' &&
            !incident.keepVehicle
        ) {
            incident.blown = true;
            vehicleById(incident.vehicleId)?.explode();
        }

        if (age >= INCIDENT_LIFETIME_MS) {
            for (const fire of firesOf(incident)) {
                fire.extinguish();
            }

            const place = incident.bar?.name ?? incident.building?.name;

            endIncident(
                incident,
                place
                    ? `the fire at ${place} burned itself out`
                    : `the fire ${incident.where} burned itself out`,
            );
        }
    }
}

/** Every call ends at once, its smoke and wrecks with it: the resource stops. */
export function endAll(): void {
    for (const incident of [...incidents.values()]) {
        endIncident(incident, null, 0);
    }
}

/** The mission change removed every fire, person and wreck. */
export function forgetAll(): void {
    incidents.clear();
    forgetBuildings();
}

events.on('fireExtinguished', (fire, info) => {
    const incident = incidentOfFire(fire);

    if (!incident || info.reason !== 'doused') {
        return;
    }

    // The main fire pays the call; each further one (a pump, a room) a little.
    const main = incident.fire.id === fire.id;
    const pay = main ? PAY[incident.kind] : SPOT_PAY;
    const last = !burning(incident);
    const crew: Player[] = [];

    for (const { player, amount, share } of info.contributors) {
        if (!player || share <= 0) {
            continue;
        }

        incident.water.set(player.id, (incident.water.get(player.id) ?? 0) + amount);
        const cut = Math.max(main ? 25 : 10, Math.round(pay * share));

        player.giveMoney(cut);
        earn(player, cut, main);

        if (main) {
            profiles.bump(player, 'firesOut');
        }

        profiles.addXp(player, main ? Math.round(20 + XP * share) : 10, 'Put out a fire');
        toast(
            player,
            last
                ? `Fire out. Your share: ${formatMoney(cut)} (${Math.round(share * 100)}% of the water).`
                : `One fire down, ${firesOf(incident).filter((entry) => entry.burning).length} still burning. ${formatMoney(cut)}.`,
            { title: 'Fire brigade', tone: 'good' },
        );

        crew.push(player);
    }

    if (crew.length && last) {
        feed('fire', [
            ...names(crew, COLORS.red),
            text(
                incident.bar || incident.building
                    ? ` put out the fire at ${incident.bar?.name ?? incident.building?.name}`
                    : ` put out the ${titleOf(incident).toLowerCase()} ${incident.where}`,
            ),
        ]);
    }

    presentIncidents();
});
