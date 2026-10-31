// Fire duty: the station, the shift and what a firefighter sees on it. The
// station is the stock models/9hasic.4ds the city scene places in the Works
// Quarter, garage doors on the street; the uniform the stock fireman's
// clothes (Hasic01); the trucks the stock fire trucks parked on the apron
// (lib/stations.ts keeps them there).
import * as jobkey from '../lib/jobkey';
import * as stations from '../lib/stations';
import { defineShift } from '../lib/shift';
import { COLORS, card, compass, feed, name, text, toast } from '../lib/notify';
import { distance, formatMinutes, formatMoney, inFreeRide, playable, plural } from '../lib/util';
import { arsonistHint, promptOfficers } from './arson';
import { buildingRows, doorHint, release as leaveBuilding } from './building';
import { clock, dispatch, updateIncidents } from './calls';
import {
    EXTINGUISHER_SECONDS,
    HOSE_TO_TRUCK,
    hoseFor,
    indoorsNow,
    nearestTruck,
    spray,
    tendExtinguisher,
} from './hose';
import { presentIncidents } from './markers';
import {
    burning,
    burningInterior,
    firesOf,
    incidentNear,
    incidents,
    intensityOf,
    targetOf,
    titleOf,
} from './model';
import { rescueNear } from './rescue';

const UNIFORM = 'Hasic01.i3d';
const TRUCK = 'fire00.i3d';
// The station on the city maps: 9hasic.4ds sits unrotated at
// (-1158.59, -5.92, 306.54) in both FREERIDE and Freeride Extended cache.bin;
// its garage doors (@VRAT02) span x -1171.4..-1142.7 at z 296.5 facing the
// street whose road graph line runs along z 284.4. Two trucks stand on the
// apron in front of the doors, noses to the street, at the apron's height
// (the building's base, -5.92): a parked car nobody drives does not settle
// onto its wheels, and a car's origin is at ground level. The sign-on arrow is
// on the apron beside them.
const CITY_STATION: stations.StationPlace = {
    signOn: { x: -1147.5, y: -5.9, z: 292.0 },
    spots: [
        { position: { x: -1158.0, y: -5.85, z: 289.5 }, heading: Math.PI },
        { position: { x: -1166.5, y: -5.85, z: 289.5 }, heading: Math.PI },
    ],
};
const STATIONS: Readonly<Record<string, stations.StationPlace>> = {
    freeride: CITY_STATION,
    freeride_extended: CITY_STATION,
    freeridenoc: CITY_STATION,
};
const TICK_MS = 250;
const CARD_MS = 1000;

export const COMPASS_PRIORITY = 44;

/** One firefighter's shift. */
export interface Crew {
    earned: number;
    fires: number;
    /** The hose on foot, theirs for the shift. */
    hose: WaterJet;
    /** The roof cannon of the truck whose wheel they hold. */
    cannon: WaterJet | null;
    cannonTruckId: number | null;
    spraying: boolean;
    /** Seconds of spray left in the extinguisher (hose.ts). */
    charge: number;
    hint: string | null;
}

/** Whether this server has fires and water jets. */
export function available(): boolean {
    return typeof Fire !== 'undefined' && typeof WaterJet !== 'undefined';
}

export const station = stations.define({
    id: 'fire',
    title: 'Fire brigade',
    crew: 'firefighters',
    mapLabel: 'Fire station',
    color: COLORS.red,
    radar: 0xd0574a,
    model: TRUCK,
    vehicleLabel: 'FIRE TRUCK',
    pitch:
        'Put out fires around the city with the hose and lead people out of them. $260 to $420 a fire, split by the water each ' +
        'firefighter puts on it, $120 for each person you lead out; a filling station, a bar or a building with people inside pays $600 to $900 in all, ' +
        'with a crew bonus. Indoors you work with the extinguisher. Press G here to sign on.',
    place: () => STATIONS[World.getMission()] ?? null,
    available,
    onDuty: (player) => crews.has(player),
    start: (player) => crews.start(player),
    stop: (player) => crews.stop(player, 'Off duty. The trucks stay at the station.'),
    onDrive: (player, truck) => takeWheel(player, truck),
    onLeave: (player) => leaveWheel(player),
});

export const crews = defineShift<Crew>({
    kind: 'fire',
    activity: 'Fire duty',
    title: 'Fire brigade',
    hud: 'fire',
    compassPriority: COMPASS_PRIORITY,
    flag: { key: 'fr.job', value: 'fire' },
    uniform: UNIFORM,
    station: {
        station,
        missing: () =>
            available()
                ? 'There is no fire station out here.'
                : 'The fire brigade needs a newer server.',
        directions: () =>
            'Sign on at the fire station in the Works Quarter: follow the compass to the arrow by the garage doors and press G.',
    },
    interrupted: 'Your shift is over.',
    tickMs: TICK_MS,

    begin(player) {
        const hose = WaterJet.create(player);

        if (!hose) {
            return 'The station has no hose free. Try again shortly.';
        }

        return {
            earned: 0,
            fires: 0,
            hose,
            cannon: null,
            cannonTruckId: null,
            spraying: false,
            charge: EXTINGUISHER_SECONDS,
            hint: null,
        };
    },

    started(player) {
        feed('fire', [name(player, COLORS.red), text(' joined the fire brigade')]);
        toast(
            player,
            'You are on duty. Get in one of the fire trucks in front of the station. Fires show on your compass and on the map (TAB). ' +
                "Near a fire, hold G to spray: on foot within 35 m of a truck, or the roof cannon from the driver's seat. " +
                'Inside a burning building you work with the extinguisher; refill it at a truck. ' +
                'G next to a trapped person leads them out. Back under the arrow, G clocks you off.',
            { title: 'Fire brigade', tone: 'gold', seconds: 12 },
        );

        if (!incidents.size) {
            clock.within(30_000);
        }
    },

    // A new fire now and then, the calls burning on, and the officers'
    // prompt to grab an arsonist.
    beforeTick(now) {
        if (!inFreeRide() || !World.isReady()) {
            return;
        }

        dispatch(now);
        updateIncidents(now);
        promptOfficers();
    },

    tick: (player, crew) => rounds(player, crew),
    afterTick: (now) => present(now),

    end(player, crew, how) {
        crew.hose.destroy();
        crew.cannon?.destroy();

        // Off duty inside a burning building: nobody else can open its door.
        if (how.online) {
            leaveBuilding(player);
        }
    },

    summary: (crew) => `You earned ${formatMoney(crew.earned)} this shift.`,
});

// At a truck's wheel the firefighter works its roof cannon; the water is theirs.
function takeWheel(player: Player, truck: Vehicle): void {
    const crew = crews.get(player);

    if (!crew || crew.cannonTruckId === truck.id) {
        return;
    }

    crew.cannon?.destroy();
    crew.cannon = WaterJet.create(truck, { pressure: 24, player });
    crew.cannonTruckId = crew.cannon ? truck.id : null;
}

function leaveWheel(player: Player): void {
    const crew = crews.get(player);

    if (!crew) {
        return;
    }

    crew.cannon?.destroy();
    crew.cannon = null;
    crew.cannonTruckId = null;
}

/** Adds to what the player earned this shift (and, for a fire's main pay, the fires), when on duty. */
export function earn(player: Player, amount: number, fire = false): void {
    const crew = crews.get(player);

    if (!crew) {
        return;
    }

    crew.earned += amount;

    if (fire) {
        crew.fires++;
    }
}

// The water, the compass and the prompt for one firefighter.
function rounds(player: Player, crew: Crew): void {
    if (!inFreeRide() || !World.isReady()) {
        return;
    }

    const alive = player.alive;

    if (!playable(player)) {
        if (!alive) {
            crew.hose.stop();
            crew.cannon?.stop();
        }

        return;
    }

    if (jobkey.isHeld(player) && crew.spraying) {
        spray(player, crew);
    }

    tendExtinguisher(player, crew, TICK_MS);
    const nearest = incidentNear(player);

    compass(
        player,
        'fire',
        COMPASS_PRIORITY,
        nearest ? { position: targetOf(nearest.incident, player) } : null,
    );

    const rescue = rescueNear(player);
    const hose = hoseFor(player, crew);
    const near =
        nearest &&
        hose &&
        (nearest.gap < hose.reach + 10 || burningInterior(player) === nearest.incident);
    let hint: string | null = null;

    const door = rescue ? null : doorHint(player);
    const indoors = indoorsNow(player);
    const charge = `${Math.round((crew.charge / EXTINGUISHER_SECONDS) * 100)}%`;

    if (rescue) {
        hint = '[G] LEAD THEM OUT';
    } else if (door) {
        hint = door;
    } else if (crew.spraying) {
        hint = indoors
            ? `EXTINGUISHER ${charge} · RELEASE G TO STOP`
            : 'SPRAYING · RELEASE G TO STOP';
    } else if (indoors && crew.charge <= 0) {
        hint = 'EXTINGUISHER EMPTY · REFILL AT A TRUCK';
    } else if (near && burning(nearest.incident)) {
        hint = indoors ? `[G] HOLD FOR THE EXTINGUISHER · ${charge}` : '[G] HOLD TO SPRAY';
    }

    hint = arsonistHint(player) ?? hint;

    if (hint !== crew.hint) {
        crew.hint = hint;
        jobkey.prompt(player, hint);
    }
}

let lastCardAt = 0;

// The markers and each firefighter's card, once a second.
function present(now: number): void {
    if (!inFreeRide() || !World.isReady() || now - lastCardAt < CARD_MS) {
        return;
    }

    lastCardAt = now;
    presentIncidents();

    for (const [player, crew] of crews.crew()) {
        card(player, 'fire', {
            title: 'Fire brigade',
            ...cardOf(player, crew, now),
            tone: 'bad',
        });
    }
}

function cardOf(
    player: Player,
    crew: Crew,
    now: number,
): { subtitle: string; rows: [string, string][] } {
    const nearest = incidentNear(player);
    const truck = station.isVehicle(player.getVehicle()) ? null : nearestTruck(player);
    const rows: [string, string][] = [];
    let subtitle =
        truck && truck.gap > HOSE_TO_TRUCK
            ? 'Get in a fire truck'
            : 'All quiet. Wait at the station.';

    if (nearest) {
        const incident = nearest.incident;
        const fires = firesOf(incident);

        rows.push([
            titleOf(incident),
            `${Math.round(nearest.gap)} m · ${Math.round(intensityOf(incident) * 100)}%`,
        ]);

        if (fires.length > 1) {
            rows.push([
                'Burning',
                `${fires.filter((fire) => fire.burning).length} of ${fires.length}`,
            ]);
        }

        const trapped = incident.civilians.filter(
            (civilian) => civilian.state === 'trapped',
        ).length;

        if (trapped) {
            rows.push([incident.bar || incident.building ? 'Inside' : 'Trapped', `${trapped}`]);
        }

        const building = buildingRows(player, incident, now);

        rows.push(...(building?.rows ?? []));

        const fuseAt = incident.pumps?.fuseAt;

        if (fuseAt) {
            rows.push(['Pumps go up in', formatMinutes(Math.max(0, fuseAt - now))]);
        }

        const arsonist = incident.arsonist;
        const loose = arsonist?.state === 'fleeing' ? arsonist.npc : null;

        if (loose) {
            rows.push([
                'Arsonist',
                `${Math.round(distance(loose.position, player.position))} m · running`,
            ]);
        }

        if (building) {
            subtitle = building.subtitle;
        } else if (incident.bar) {
            subtitle =
                burningInterior(player) === incident
                    ? 'Put the room out, lead them to the exit arrow'
                    : `People inside ${incident.bar.name}: in through the door`;
        } else if (fuseAt) {
            subtitle = 'Knock the flames down before the pumps go up';
        } else if (!burning(incident) && arsonist?.state === 'fleeing') {
            subtitle = 'Catch the arsonist: G next to him';
        } else {
            subtitle = `A fire ${incident.where}`;
        }
    }

    if (truck) {
        rows.push(['Nearest truck', `${Math.round(truck.gap)} m`]);
    }

    if (indoorsNow(player) || crew.charge < EXTINGUISHER_SECONDS) {
        rows.push([
            'Extinguisher',
            crew.charge > 0
                ? `${Math.round((crew.charge / EXTINGUISHER_SECONDS) * 100)}%`
                : 'empty · refill at a truck',
        ]);
    }

    rows.push(['This shift', `${plural(crew.fires, 'fire')} · ${formatMoney(crew.earned)}`]);

    return { subtitle, rows };
}
