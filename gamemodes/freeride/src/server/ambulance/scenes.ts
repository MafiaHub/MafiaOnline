// Where the calls come from: the street (someone collapsed, someone badly
// hurt, now and then a woman in labour, a heart attack or a pile-up), the
// fire brigade's smoke victims, and players who went down while a medic is
// on duty: they wait for one before the usual respawn (lib/life.ts holds it).
import * as barRoom from '../lib/bar_room';
import * as firefighters from '../firefighters';
import * as life from '../lib/life';
import * as prison from '../lib/prison';
import * as hospitalRoom from '../lib/hospital_room';
import * as houseRooms from '../lib/house_rooms';
import { CallClock, nearestGap, roadPointNear, roadside, spawnWreck } from '../lib/callouts';
import { CIVILIANS, CROUCHED, LYING, MEN, WOMEN, spawnCivilian } from '../lib/civilians';
import { npcsAvailable } from '../lib/npcs';
import { COLORS, feed, text, toast } from '../lib/notify';
import { events } from '../lib/lifecycle';
import { MAIN_WORLD, inFreeRide, pick, type Point } from '../lib/util';
import {
    DOWN_MS,
    calls,
    clearWrecks,
    openCall,
    openPileup,
    pileups,
    presentCalls,
    type Call,
    type Pileup,
    type Triage,
} from './board';
import { medics } from './duty';
import { hospital } from './places';

const CALL_EVERY_MS = 3 * 60_000;
const FIRST_CALL_MS = 45_000;
const MAX_CALLS = 3;
// How long a heart attack and a pile-up's critical patient last untreated;
// longer when the nearest medic is far.
const CARDIAC_MS = 4 * 60_000;
const BLEED_MS = 3.5 * 60_000;
// Activities (lib/life.ts) whose players still wait for a medic.
const JOBS = new Set<string>(['fire', 'medic', 'taxi', 'duty', 'courier', 'bootleg', 'farm']);

export const clock = new CallClock(FIRST_CALL_MS);

export type StreetCallKind = 'minor' | 'serious' | 'labour' | 'cardiac' | 'pileup';

// What comes in, by how often: mostly someone hurt, now and then the rest.
const DRAW: readonly StreetCallKind[] = [
    'minor',
    'minor',
    'serious',
    'serious',
    'labour',
    'cardiac',
    'pileup',
];
// The last call's kind, which the next one is not.
let lastKind: StreetCallKind | null = null;

function medicPlayers(): Player[] {
    return medics.crew().map(([player]) => player);
}

/** A new street call now and then while medics are on duty, near one of them. */
export function dispatch(now: number): void {
    if (!medics.size || !hospital() || !clock.due(now, CALL_EVERY_MS)) {
        return;
    }

    const anchor: Player | undefined = pick(medicPlayers());
    const fresh = DRAW.filter((kind) => kind !== lastKind);
    const kind = pick(fresh.length ? fresh : DRAW);

    if (streetCall(anchor?.position ?? null, kind)) {
        lastKind = kind;
    }
}

function spawnPatient(model: string, position: Point): Npc | null {
    return spawnCivilian(model, position, Math.random() * Math.PI * 2);
}

// How long an urgent patient lasts: the call's own time, or more when the
// nearest medic is far off (about 12 m/s there and two minutes to work).
function timeToLive(position: Point, ms: number): number {
    const gap = nearestGap(medicPlayers(), position);

    return Math.max(ms, gap === null ? 0 : (gap / 12) * 1000 + 120_000);
}

/** A street call: someone on the pavement by a road. Now and then a woman in labour, a heart attack or a pile-up. */
export function streetCall(
    origin: Point | null,
    kind: StreetCallKind = pick(DRAW),
): Call | Pileup | null {
    if (!npcsAvailable() || calls.size >= MAX_CALLS) {
        return null;
    }

    if (kind === 'pileup') {
        return pileup(origin) ?? streetCall(origin, 'serious');
    }

    // On the pavement, 4.5 m off the road graph line.
    const spot = roadside(origin, 200, 1000, 4.5).position;
    const position = { x: spot.x, y: spot.y + 0.2, z: spot.z };
    const model =
        kind === 'labour' ? pick(WOMEN) : kind === 'cardiac' ? pick(MEN) : pick(CIVILIANS);
    const npc = spawnPatient(model, position);

    if (!npc) {
        return null;
    }

    if (kind === 'labour') {
        return openCall('labour', position, 'a woman in labour', { npc, clip: CROUCHED });
    }

    if (kind === 'cardiac') {
        return openCall('cardiac', position, 'a heart attack', {
            npc,
            clip: LYING,
            expires: Date.now() + timeToLive(position, CARDIAC_MS),
        });
    }

    return openCall(kind, position, 'a civilian', { npc });
}

/**
 * A pile-up: two wrecks across the lane and three people hurt around them,
 * on the pavements either side (4.5 to 5 m off the road graph line, as the
 * street calls lie). With the fire brigade on duty one wreck burns beside
 * them (firefighters/calls.ts).
 */
function pileup(origin: Point | null): Pileup | null {
    if (calls.size > MAX_CALLS - 1 || [...pileups.values()].some((group) => !group.clearAt)) {
        return null;
    }

    const spot = roadPointNear(origin, 200, 1000);
    const { x, y, z } = spot.position;
    const { direction } = spot;
    const heading = Math.atan2(direction.x, direction.z);
    const along = (metres: number, side = 0): Point => ({
        x: x + direction.x * metres + direction.z * side,
        y,
        z: z + direction.z * metres - direction.x * side,
    });
    const wrecks = [
        spawnWreck(along(3.5, 1.2), heading + 0.7),
        spawnWreck(along(-3.0, -1.0), heading + Math.PI - 0.5),
    ].flatMap((car) => (car ? [car.id] : []));

    if (!wrecks.length) {
        return null;
    }

    const group = openPileup(along(0), wrecks);
    const side = Math.random() < 0.5 ? -1 : 1;
    const patients: [Triage, Point, string][] = [
        ['critical', along(1.0, 5 * side), LYING],
        ['hurt', along(-4.0, 5 * side), LYING],
        ['minor', along(5.5, -5 * side), CROUCHED],
    ];

    for (const [triage, position, clip] of patients) {
        const npc = spawnPatient(pick(CIVILIANS), {
            x: position.x,
            y: position.y + 0.2,
            z: position.z,
        });

        if (!npc) {
            continue;
        }

        const critical = triage === 'critical';
        const call = openCall(critical ? 'serious' : 'minor', position, 'a crash victim', {
            npc,
            quiet: true,
            clip,
            triage,
            pileupId: group.id,
            expires: critical ? Date.now() + timeToLive(position, BLEED_MS) : null,
        });

        group.callIds.add(call.id);
    }

    if (!group.callIds.size) {
        clearWrecks(group);

        return null;
    }

    // With the fire brigade on duty, the first wreck burns beside them.
    if (firefighters.crewSize()) {
        group.fireId =
            firefighters.startIncident('pileup', null, {
                position: along(3.5, 1.2),
                vehicleId: group.wrecks[0],
            })?.id ?? null;
    }

    for (const [medic] of medics.crew()) {
        toast(
            medic,
            `A pile-up ${group.where}: ${group.callIds.size} people hurt around two wrecks. The critical one first, they are ` +
                `bleeding out; the others you patch up where they sit.${group.fireId ? ' A wreck is burning: the fire brigade is on its way.' : ''}`,
            { title: 'Ambulance call', tone: 'gold', seconds: 10 },
        );
    }

    feed('medic', [
        text('Ambulance', COLORS.green),
        text(` · a pile-up ${group.where}, ${group.callIds.size} hurt`),
    ]);

    presentCalls();

    return group;
}

// A player who goes down while a medic is on duty waits for one.
events.on('playerDeath', (player, killer, info) => {
    // Not in a duel, race or other contest; a job is fine. Not after /kill,
    // which asks for the respawn, and not for the only medic.
    const busy = life.activity(player);

    if (
        !hospital() ||
        !inFreeRide() ||
        player.virtualWorld !== MAIN_WORLD ||
        (busy && !JOBS.has(busy.kind)) ||
        info.cause === 'script'
    ) {
        return;
    }

    if (!medics.crew().some(([medic]) => medic.id !== player.id)) {
        return;
    }

    // Rooms built apart under the fields (the back-room bar, the jail, the
    // hospital wards, the homes and the burning buildings in their rooms):
    // the interior has already put the player back in the street world, but
    // no ambulance reaches them there.
    if (
        barRoom.inRoom(player.position, 1) ||
        prison.inHall(player.position, 2) ||
        hospitalRoom.inRoom(player.position, 1) ||
        houseRooms.roomAt(player.position, 1) !== null
    ) {
        return;
    }

    if ([...calls.values()].some((call) => call.kind === 'downed' && call.playerId === player.id)) {
        return;
    }

    life.holdRespawn(player, Date.now() + DOWN_MS);
    openCall('downed', player.position, player.nickname, {
        playerId: player.id,
        killerId: killer instanceof Player ? killer.id : null,
    });

    toast(
        player,
        'A medic has been called. Hold on for a minute, or press G to wake up at the hospital.',
        { title: 'Ambulance', tone: 'gold', seconds: 8 },
    );
});

// Smoke victims the fire brigade led out (firefighters/rescue.ts).
firefighters.setAmbulance({
    // options.bonus: a share of a big fire call's bonus, paid with the fare.
    takePatient(npc, position, label, options = {}) {
        if (!medics.size || !hospital() || calls.size >= MAX_CALLS + 2) {
            return false;
        }

        npc.stop();
        // Coughing on the kerb, not lying in the road.
        const call = openCall('serious', npc.position ?? position, label, {
            npc,
            clip: CROUCHED,
            bonus: options.bonus ?? 0,
        });

        call.ready = true;

        return true;
    },
});
