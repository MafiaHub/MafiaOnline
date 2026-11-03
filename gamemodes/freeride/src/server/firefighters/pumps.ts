// A filling station alight: a car burning at the pumps, which go up if the
// flames are not knocked down in time.
import * as maps from '../lib/maps';
import { nearestGap, spawnWreck } from '../lib/callouts';
import { COLORS, feed, text, toast, toastAll } from '../lib/notify';
import { flatDistance, onStreet, pick, vehicleById, type Point } from '../lib/util';
import { crews } from './duty';
import { presentIncidents } from './markers';
import { firesOf, incidents, newIncident, type Incident, type PumpFire } from './model';

// The stock Free Ride scenes' three filling stations, the same in FREERIDE
// (a1.dta) and freeride_extended (the mod's scene2.bin): the pump
// ("_pumpa_pumpaN", models/xpumpa 2.4ds) and the nearest road graph point
// (data/roads_<map>.js), 9 to 14 m off. The burning car stands on the
// forecourt between them, 40% of the way from the pump to the road. The
// first is the Night Pump of server/shops.ts.
const PUMPS: readonly { pump: Point; road: Point }[] = [
    { pump: { x: -109.22, y: 8.52, z: -136.51 }, road: { x: -100.79, y: 8.49, z: -146.09 } },
    { pump: { x: -1381.2, y: -6.81, z: -61.73 }, road: { x: -1381.22, y: -6.86, z: -52.56 } },
    { pump: { x: -1784.15, y: -4.85, z: -210.74 }, road: { x: -1784.01, y: -4.63, z: -197.07 } },
];
// The pumps go up this long after the call, or later for a crew far away;
// unless every flame there is down to this.
const PUMP_FUSE_MS = 150_000;
const PUMP_SAFE_INTENSITY = 0.3;

export function startPumpFire(origin: Point | null): Incident | null {
    if (!crews.size || !maps.current()?.shops) {
        return null;
    }

    if ([...incidents.values()].some((incident) => incident.kind === 'pump')) {
        return null;
    }

    const choices = PUMPS.filter((entry) => !origin || flatDistance(entry.pump, origin) >= 150);
    const { pump, road } = pick(choices.length ? choices : PUMPS);
    const heading = Math.atan2(road.x - pump.x, road.z - pump.z) + Math.PI / 2;
    const forecourt = {
        x: pump.x + (road.x - pump.x) * 0.4,
        y: pump.y,
        z: pump.z + (road.z - pump.z) * 0.4,
    };
    const car = spawnWreck(forecourt, heading);

    if (!car) {
        return null;
    }

    const fire = Fire.create(
        { x: forecourt.x, y: forecourt.y + 0.4, z: forecourt.z },
        { radius: 2.6, height: 1, toughness: 2.5, growth: 0.012, damage: 15, attachTo: car },
    );
    const pumpFire = Fire.create(pump, {
        radius: 2.2,
        height: 3,
        toughness: 3,
        growth: 0.015,
        damage: 18,
    });

    if (!fire || !pumpFire) {
        fire?.destroy();
        pumpFire?.destroy();
        car.destroy();

        return null;
    }

    // Long enough for the nearest crew to drive there and get the hose out.
    const nearest = nearestGap(
        crews.crew().map(([player]) => player),
        forecourt,
    );
    const pumps: PumpFire = {
        pump,
        fuseAt:
            Date.now() +
            Math.max(PUMP_FUSE_MS, (nearest === null ? 0 : nearest / 14) * 1000 + 90_000),
        warned: false,
    };
    const incident = newIncident('pump', forecourt, 'FILLING STATION FIRE', fire, {
        vehicleId: car.id,
        extra: [pumpFire],
        pumps,
    });

    incidents.set(incident.id, incident);
    toastAll(
        `A car caught fire at the pumps of the filling station ${incident.where}. Knock the flames down before the pumps go up!`,
        { title: 'Fire', tone: 'bad', seconds: 8 },
    );

    feed('fire', [
        text('Fire', COLORS.red),
        text(` · the filling station ${incident.where} is burning`),
    ]);

    presentIncidents();

    return incident;
}

// Time is up at the pumps: what still burns there takes the fuel with it.
function pumpsGoUp(incident: Incident, pumps: PumpFire): void {
    incident.blown = true;
    vehicleById(incident.vehicleId)?.explode();
    const store = Fire.create(pumps.pump, {
        radius: 5,
        height: 6,
        toughness: 5,
        growth: 0.02,
        damage: 25,
    });

    if (store) {
        incident.extra.push(store);
    }

    incident.label = 'FILLING STATION FIRE · PUMPS GONE UP';

    for (const player of World.getPlayers().filter(onStreet)) {
        if (crews.has(player) || flatDistance(player.position, incident.position) < 300) {
            toast(player, `The pumps ${incident.where} went up. Put out what is left.`, {
                title: 'Fire',
                tone: 'bad',
                seconds: 6,
            });
        }
    }

    feed('fire', [text('Fire', COLORS.red), text(` · the pumps ${incident.where} went up`)]);
    presentIncidents();
}

/** The pumps: safe once every flame there is down low, else they go up. */
export function updatePumps(incident: Incident, now: number): void {
    const pumps = incident.pumps;

    if (!pumps?.fuseAt) {
        return;
    }

    const left = pumps.fuseAt - now;

    if (firesOf(incident).every((fire) => !fire.burning || fire.intensity <= PUMP_SAFE_INTENSITY)) {
        pumps.fuseAt = null;
        tellCrew(`The pumps ${incident.where} are safe. Finish the fire off.`, 'good');
    } else if (left <= 0) {
        pumps.fuseAt = null;
        pumpsGoUp(incident, pumps);
    } else if (left <= 30_000 && !pumps.warned) {
        pumps.warned = true;
        tellCrew(`Thirty seconds before the pumps ${incident.where} go up!`, 'bad');
    }
}

function tellCrew(message: string, tone: 'good' | 'bad'): void {
    for (const [player] of crews.crew()) {
        toast(player, message, { title: 'Fire brigade', tone, seconds: 4 });
    }
}
