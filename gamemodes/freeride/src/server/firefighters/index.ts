// The fire brigade. A player signs on at the fire station (the stock
// models/9hasic.4ds the city scene places in the Works Quarter, garage doors
// on the street: stand under the arrow on the apron and press G), puts on the
// stock fireman's clothes (Hasic01) and takes one of the stock fire trucks
// parked in front of the doors (lib/stations.ts keeps them there; duty.ts).
// Fires break out across the city on the road graph: a house front, a
// burning car, a crash with people trapped in it (calls.ts). Firefighters put
// them out with the hose (hold G within reach of any fire truck, or from a
// truck's roof cannon while at its wheel; hose.ts): the native water jet
// arcs onto the fire and every hose on it takes the flames down, so a crew
// is faster than one. Trapped civilians are led out with G (rescue.ts).
// Everyone sees the fires, the crew on duty on the city map too
// (markers.ts); the pay for one is split by how much water each firefighter
// put on it.
//
// With a crew on duty bigger calls come in too: a filling station alight,
// whose pumps go up if the flames are not knocked down in time (pumps.ts); a
// fire in a bar, where the crew goes in through the door, puts the room out
// with the extinguisher and leads the barman and the drinkers out to the
// street (bar.ts; the medics take them from there); a home burning with its
// residents inside, where the crew searches the furnished rooms, puts the
// furniture out by hand and gets them out before the fire spreads and the
// smoke takes them (building.ts); and a house set alight on purpose, its arsonist
// running off down the street until a firefighter or a police officer on
// duty grabs him (arson.ts). A pile-up the ambulance answers (ambulance/)
// may have a wreck burning.
import * as commands from '../lib/commands';
import * as serverConsole from '../lib/console';
import * as log from '../lib/log';
import * as jobkey from '../lib/jobkey';
import * as menu from '../lib/menu';
import { WRECKS } from '../lib/callouts';
import { compass, toast } from '../lib/notify';
import { events, onStop } from '../lib/lifecycle';
import { grabKey } from './arson';
import { release as leaveBuilding } from './building';
import { clock, endAll, forgetAll, startIncident } from './calls';
import { COMPASS_PRIORITY, crews, station } from './duty';
import { crewKey } from './hose';
import {
    burning,
    incidents as open,
    nearestIncident,
    type Incident,
    type IncidentKind,
} from './model';
import { setAmbulance } from './rescue';

// Grabbing an arsonist (firefighters and police officers on duty) goes
// before the brigade's own use of G.
jobkey.register(grabKey);
jobkey.register(crewKey);

// Signing on happens under the station's arrow; the command shows the way
// there and clocks off from anywhere.
commands.register(
    {
        name: 'fire',
        aliases: ['firefighter', 'fireman'],
        usage: '/fire',
        help: 'Show the way to the fire station, or clock off.',
        category: 'City',
    },
    (player) => {
        if (crews.has(player)) {
            crews.stop(player, 'Off duty.');
        } else {
            crews.start(player);
        }
    },
);

commands.register(
    { name: 'fires', usage: '/fires', help: 'Where the city is burning.', category: 'City' },
    (player) => {
        const list = [...open.values()].filter(burning);

        if (!list.length) {
            toast(player, 'No fires right now.', { title: 'Fire brigade' });

            return;
        }

        const nearest = nearestIncident(player.position);

        if (nearest) {
            compass(player, 'fire', COMPASS_PRIORITY, { position: nearest.incident.position });
        }

        toast(player, list.map((incident) => `${incident.label} ${incident.where}`).join(' · '), {
            title: 'Fires',
            tone: 'bad',
        });
    },
);

menu.provide('fire', (player) => ({
    onDuty: crews.has(player),
    crew: crews.size,
    available: Boolean(station.here()),
    fires: [...open.values()].filter(burning).length,
    earned: crews.get(player)?.earned ?? 0,
}));

const KINDS: readonly IncidentKind[] = ['building', 'house', 'car', 'crash', 'pump', 'bar'];

// "fr fire [kind]": a fire now, near a firefighter on duty (else anyone on
// the street), for trying the calls out. A building or a bar needs a crew on duty.
serverConsole.action('fr', 'fire', ([kind]) => {
    const chosen = KINDS.find((entry) => entry === kind) ?? 'building';
    const anchor = crews.crew()[0]?.[0] ?? World.getPlayers()[0];
    const incident = startIncident(chosen, anchor?.position ?? null);

    log.info(
        incident
            ? `Started a ${chosen} fire ${incident.where}`
            : `No ${chosen} fire could start (kinds: ${KINDS.join(', ')}; a building or a bar needs a firefighter on duty, two calls at most)`,
    );
});

// After the shift's own (duty.ts): the crew is off duty before the calls go.
onStop(endAll);
events.on('missionChange', forgetAll);
// Dead inside a burning building: out of its world before the respawn.
events.on('playerDeath', (player) => leaveBuilding(player));

export function onDuty(player: Player): boolean {
    return crews.has(player);
}

export function crewSize(): number {
    return crews.size;
}

/** The calls the brigade has open. */
export function incidents(): Incident[] {
    return [...open.values()];
}

/** The next fire breaks out `ms` from now. */
export function schedule(ms: number): void {
    clock.after(ms);
}

export { WRECKS, setAmbulance, startIncident };
export type { AmbulanceDesk } from './rescue';
export type { PileupWreck } from './calls';
export type { Incident };
