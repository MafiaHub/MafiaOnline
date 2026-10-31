// Visiting hours at the county jail. An arrow outside the gate of the city's
// prison building (jail.visit in lib/maps.ts) leads into the jail's visiting
// area: the corridor side of the bars, in the jail's virtual world, where
// visitors can see and talk to the prisoners and the warden. The guard holds
// a visitor's weapons at the door and hands them back on the way out, keeps
// visitors out of the cells, and turns away anyone the police want or who is
// busy with something else (police duty is welcome). Visitors free nobody:
// only a served sentence or bail opens a cell (jail/prisoners.ts).
import * as life from '../lib/life';
import * as maps from '../lib/maps';
import * as prison from '../lib/prison';
import * as warden from '../lib/warden';
import { events, every, onStart, onStop } from '../lib/lifecycle';
import { toast, arrow, markers, markersAll, COLORS, type Marker } from '../lib/notify';
import { PlayerMap } from '../lib/players';
import { WalkIn } from '../lib/spots';
import { Transfers } from '../lib/transfer';
import { MAIN_WORLD, playable, type Point } from '../lib/util';
import { exitArrow, street, unarmed } from './common';
import { jailed } from './prisoners';

const TICK_MS = 250;
const TRIGGER_RADIUS = 1.4;
const REARM_DISTANCE = 3;
const MARKER_RANGE = 60;

interface Visit {
    /** What the guard holds until the visitor leaves. */
    loadout: life.Loadout;
    /** A police officer on duty visits without leaving the shift. */
    duty: boolean;
}

const visitors = new PlayerMap<Visit>();
// One spot at a time per player: the entrance outside, the exit inside.
const doorway = new WalkIn(TRIGGER_RADIUS, REARM_DISTANCE);
const moves = new Transfers();

export function visiting(player: Player): boolean {
    return visitors.has(player.id);
}

function entrance(): Point | null {
    return maps.current()?.jail.visit?.position ?? null;
}

function publish(player: Player | null = null): void {
    const spot = entrance();
    const items: Marker[] = spot
        ? [arrow('jail', spot, 'COUNTY JAIL · VISITS', COLORS.blue, MARKER_RANGE)]
        : [];

    if (player) {
        markers(player, 'jail-entrance', items);
    } else {
        markersAll('jail-entrance', items);
    }
}

function refused(player: Player): string | null {
    if (!prison.isBuilt()) {
        return 'Visiting hours are over.';
    }

    if ((Police.getRecord(player)?.level ?? 0) > 0) {
        return 'The guard will not let a wanted man in.';
    }

    const current = life.activity(player);

    return current && current.kind !== 'duty' ? life.busyMessage(player) : null;
}

function enter(player: Player): void {
    const problem = refused(player);

    if (problem) {
        toast(player, problem, { tone: 'bad', seconds: 3 });

        return;
    }

    const loadout = life.loadoutOf(player);
    const duty = life.activity(player)?.kind === 'duty';

    moves.move(player, prison.visit(), prison.WORLD, {
        loadout: unarmed(player),
        then: (moved) => {
            if (!moved) {
                return;
            }

            visitors.set(player.id, { loadout, duty });

            if (!duty) {
                life.begin(player, 'visit', 'Visiting the jail');
            }

            doorway.disarm(player);
            markers(player, 'jail-visit', [exitArrow()]);
            toast(player, 'Visiting hours. The guard holds your guns until you leave.', {
                title: 'County jail',
                seconds: 6,
            });
        },
    });
}

function forget(player: Player): void {
    if (!visitors.delete(player.id)) {
        return;
    }

    life.end(player, 'visit');
    markers(player, 'jail-visit', []);
}

function leave(player: Player, visit: Visit): void {
    const spot = street();

    if (!spot) {
        return;
    }

    moves.move(player, spot, MAIN_WORLD, {
        loadout: { ...visit.loadout, health: player.health },
        then: (moved) => {
            if (!moved) {
                return;
            }

            forget(player);
            doorway.disarm(player);
            toast(player, 'The guard hands your guns back.', { title: 'County jail' });
        },
    });
}

function watchVisitor(player: Player, visit: Visit): void {
    // Arrested while inside: jail/prisoners.ts has them now.
    if (jailed(player)) {
        forget(player);

        return;
    }

    if (prison.inAnyCell(player.position)) {
        const { position, heading } = prison.visit();

        life.teleport(player, position, heading, unarmed(player));
        toast(player, 'The guard keeps visitors out of the cells.', { tone: 'bad', seconds: 3 });

        return;
    }

    if (doorway.reached(player, prison.exit())) {
        leave(player, visit);
    }
}

every(TICK_MS, () => {
    warden.need('visitors', visitors.size > 0);
    const spot = entrance();

    for (const player of World.getPlayers()) {
        if (moves.isMoving(player) || !player.alive) {
            continue;
        }

        const visit = visitors.get(player.id);

        if (visit) {
            watchVisitor(player, visit);
        } else if (
            spot &&
            playable(player) &&
            player.virtualWorld === MAIN_WORLD &&
            !player.getVehicle() &&
            !jailed(player) &&
            doorway.reached(player, spot)
        ) {
            enter(player);
        }
    }
});

events.on('playerSpawn', (player) => {
    if (!life.isTransit(player)) {
        publish(player);
    }
});

// A visitor who dies wakes up in the street world without the guns they left.
events.on('playerDeath', (player) => {
    if (!visitors.has(player.id)) {
        return;
    }

    forget(player);

    if (!jailed(player)) {
        player.setVirtualWorld(MAIN_WORLD);
    }
});

// The mission change took the jail down: its visitors leave its world and
// the visit.
events.on('missionChange', () => {
    for (const player of World.getPlayers()) {
        if (!visitors.has(player.id)) {
            continue;
        }

        forget(player);

        if (player.virtualWorld === prison.WORLD) {
            player.setVirtualWorld(MAIN_WORLD);
        }
    }

    visitors.clear();
    moves.clear();
    publish();
});

onStart(() => publish());

onStop(() => {
    for (const player of World.getPlayers()) {
        const visit = visitors.get(player.id);

        if (!visit) {
            continue;
        }

        player.setVirtualWorld(MAIN_WORLD);
        const spot = street();

        if (player.alive && spot) {
            life.teleport(player, spot.position, spot.heading, {
                ...visit.loadout,
                health: player.health,
            });
        }
    }

    visitors.clear();
});
