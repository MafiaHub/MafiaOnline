// The arsonist: a hood (the stock Gangster01, one of Salieri's men in
// server/bars/) who runs from a house he set alight, down the street until a
// firefighter or a police officer on duty grabs him (G next to him) or
// knocks him down.
import * as jobkey from '../lib/jobkey';
import * as policeDuty from '../police_duty';
import * as profiles from '../lib/profiles';
import { roadside } from '../lib/callouts';
import { CROUCHED, GIVE_UP_LINES, say, spawnCivilian } from '../lib/civilians';
import { walkOff } from '../lib/npcs';
import { COLORS, compass, feed, name, text, toast } from '../lib/notify';
import { events, onStop } from '../lib/lifecycle';
import { PlayerMap } from '../lib/players';
import {
    distance,
    flatDistance,
    formatMoney,
    onStreet,
    playable,
    playerById,
    type Placement,
} from '../lib/util';
import { crews, earn } from './duty';
import { presentIncidents } from './markers';
import { incidents, type ArsonistState, type Incident } from './model';

const ARSONIST_MODEL = 'Gangster01.i3d';
const ARSONIST_VOICE = 13;

export const ARSONIST_CHANCE = 0.4;
const ARSONIST_MS = 100_000;
const ARSONIST_PAY = 150;
const ARSONIST_XP = 50;
const CATCH_REACH = 3;
const ARSON_ALERT_RANGE = 1500;
const COMPASS_PRIORITY = 46;

// Officers on duty shown the grab prompt, by player id.
const officerPrompts = new PlayerMap<string | null>({ clearOnMissionChange: true });

/** He runs from the house at `spot`; the officers near enough are told. */
export function spawnArsonist(incident: Incident, spot: Placement): void {
    const heading = spot.heading;
    const position = {
        x: spot.position.x + Math.sin(heading) * 6,
        y: spot.position.y + 0.2,
        z: spot.position.z + Math.cos(heading) * 6,
    };
    const npc = spawnCivilian(ARSONIST_MODEL, position, heading, { voice: ARSONIST_VOICE });

    if (!npc) {
        return;
    }

    const arsonist = {
        npc,
        state: 'fleeing' as const,
        ready: false,
        task: 0,
        until: Date.now() + ARSONIST_MS,
        alerted: new Set<number>(),
    };

    incident.arsonist = arsonist;

    for (const player of World.getPlayers().filter(onStreet)) {
        if (
            !policeDuty.onDuty(player) ||
            flatDistance(player.position, position) > ARSON_ALERT_RANGE
        ) {
            continue;
        }

        arsonist.alerted.add(player.id);
        toast(
            player,
            `An arsonist is running from a house fire ${incident.where}. Your compass shows him: grab him with G, or knock him down.`,
            { title: 'Police radio', tone: 'gold', seconds: 8 },
        );
    }
}

// Away down the street, at a run; again when he gets there.
function flee(incident: Incident): void {
    const arsonist = incident.arsonist;

    if (!arsonist?.npc || arsonist.state !== 'fleeing' || !arsonist.ready) {
        return;
    }

    const away = roadside(arsonist.npc.position, 120, 260, 4.5);

    arsonist.task = arsonist.npc.walkTo(away.position, { run: true, radius: 3 });
}

function arsonistNear(player: Player): Incident | null {
    for (const incident of incidents.values()) {
        const arsonist = incident.arsonist;

        if (
            arsonist?.state === 'fleeing' &&
            arsonist.npc &&
            distance(arsonist.npc.position, player.position) <= CATCH_REACH
        ) {
            return incident;
        }
    }

    return null;
}

/** Firefighters and police officers on duty may grab him, and are paid for it. */
function canCatch(player: Player): boolean {
    return crews.has(player) || Boolean(policeDuty.onDuty(player));
}

/** The prompt for someone who may grab him and stands next to him. */
export function arsonistHint(player: Player): string | null {
    return canCatch(player) && !player.getVehicle() && arsonistNear(player) ? '[G] GRAB HIM' : null;
}

function endArson(incident: Incident, state: ArsonistState): void {
    const arsonist = incident.arsonist;

    if (!arsonist || arsonist.state !== 'fleeing') {
        return;
    }

    arsonist.state = state;

    for (const id of arsonist.alerted) {
        const player = playerById(id);

        if (player) {
            compass(player, 'arson', COMPASS_PRIORITY, null);
        }
    }

    arsonist.alerted.clear();
}

/** paid: a firefighter or an officer on duty caught him; anyone else only stops him. */
function catchArsonist(incident: Incident, player: Player, paid = true): void {
    const arsonist = incident.arsonist;

    if (!arsonist) {
        return;
    }

    endArson(incident, 'caught');
    const npc = arsonist.npc;

    arsonist.npc = null;

    if (npc) {
        npc.playAnimation(CROUCHED, { loop: true });
        say(npc, GIVE_UP_LINES);
        // The police take him from here.
        walkOff(npc, 15_000);
    }

    if (paid) {
        player.giveMoney(ARSONIST_PAY);
        earn(player, ARSONIST_PAY);
        profiles.addXp(player, ARSONIST_XP, 'Caught an arsonist');
        toast(
            player,
            `Got him. ${formatMoney(ARSONIST_PAY)} from the city; the police take him from here.`,
            { title: 'Arson', tone: 'good' },
        );
    } else {
        toast(
            player,
            "You stopped the arsonist. The police take him from here; the city's reward is for the brigade and officers on duty.",
            { title: 'Arson', tone: 'gold' },
        );
    }

    feed('fire', [name(player, COLORS.red), text(` caught the arsonist ${incident.where}`)]);
    presentIncidents();
}

function arsonistGone(incident: Incident, reason: 'escaped' | 'dead'): void {
    const arsonist = incident.arsonist;

    if (!arsonist || arsonist.state !== 'fleeing') {
        return;
    }

    endArson(incident, reason);
    const npc = arsonist.npc;

    arsonist.npc = null;

    if (reason === 'escaped') {
        npc?.remove();
        feed('fire', [
            text('Fire', COLORS.muted),
            text(` · the arsonist ${incident.where} got away`),
        ]);
    } else if (npc) {
        feed('fire', [
            text('Fire', COLORS.muted),
            text(` · the arsonist ${incident.where} is dead`),
        ]);

        walkOff(npc, 30_000);
    }

    presentIncidents();
}

/** The call is over: he got away, if he was still running. */
export function dismissArsonist(incident: Incident): void {
    const arsonist = incident.arsonist;

    if (!arsonist) {
        return;
    }

    endArson(incident, 'escaped');
    arsonist.npc?.remove();
    arsonist.npc = null;
}

/** Each tick: gone when his time is up, else on the alerted officers' compass. */
export function updateArsonist(incident: Incident, now: number): void {
    const arsonist = incident.arsonist;

    if (arsonist?.state !== 'fleeing') {
        return;
    }

    if (now >= arsonist.until || !arsonist.npc) {
        arsonistGone(incident, 'escaped');

        return;
    }

    for (const id of arsonist.alerted) {
        const officer = playerById(id);

        if (officer) {
            compass(officer, 'arson', COMPASS_PRIORITY, { position: arsonist.npc.position });
        }
    }
}

/** Officers on duty grab an arsonist with G too. */
export function promptOfficers(): void {
    for (const player of World.getPlayers()) {
        if (crews.has(player) || !policeDuty.onDuty(player)) {
            continue;
        }

        const hint = playable(player) ? arsonistHint(player) : null;

        if (hint !== (officerPrompts.get(player.id) ?? null)) {
            officerPrompts.set(player.id, hint);
            jobkey.prompt(player, hint);
        }
    }
}

/** G next to him, for firefighters and police officers on duty. */
export const grabKey: jobkey.JobKeyHandler = {
    press(player) {
        if (!playable(player) || player.getVehicle() || !canCatch(player)) {
            return false;
        }

        const incident = arsonistNear(player);

        if (!incident) {
            return false;
        }

        catchArsonist(incident, player);

        return true;
    },
};

function arsonOf(npc: Npc): Incident | null {
    for (const incident of incidents.values()) {
        if (incident.arsonist?.npc?.id === npc.id) {
            return incident;
        }
    }

    return null;
}

events.on('npcSpawn', (npc) => {
    const incident = arsonOf(npc);

    if (incident?.arsonist) {
        incident.arsonist.ready = true;
        flee(incident);
    }
});

// He keeps running while he is loose.
events.on('npcTaskComplete', (npc, info) => {
    const incident = arsonOf(npc);

    if (incident && info.id === incident.arsonist?.task && info.result !== 'cancelled') {
        flee(incident);
    }
});

// Hit by a player, or knocked over by a player's car, he gives up. The
// city pays only a firefighter or an officer on duty for him.
events.on('npcDamage', (npc, info) => {
    if (info.healthAfter <= 0) {
        return;
    }

    const attacker = info.attacker instanceof Vehicle ? info.attacker.getDriver() : info.attacker;

    if (!(attacker instanceof Player)) {
        return;
    }

    const incident = arsonOf(npc);

    if (incident?.arsonist?.state === 'fleeing') {
        catchArsonist(incident, attacker, canCatch(attacker));
    }
});

events.on('npcDeath', (npc) => {
    const incident = arsonOf(npc);

    if (incident) {
        arsonistGone(incident, 'dead');
    }
});

events.on('npcRetire', (npc) => {
    const incident = arsonOf(npc);

    if (incident) {
        arsonistGone(incident, 'escaped');
    }
});

onStop(() => {
    for (const [id, hint] of officerPrompts) {
        const player = playerById(id);

        if (player && hint) {
            jobkey.prompt(player, null);
        }
    }

    officerPrompts.clear();
});
