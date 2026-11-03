// A job shift: the part every duty job (fire brigade, ambulance, farm, police,
// taxi) shares. Going on duty takes the player's one exclusive activity
// (lib/life.ts), may put them in a uniform and sign them on at a station
// (lib/stations.ts); going off gives back what they wore and carried and
// clears what the job put on their HUD. A shift ends when the player leaves,
// when the mission changes and when the resource stops; the job only says
// what is its own.
import * as jobkey from './jobkey';
import * as life from './life';
import * as profiles from './profiles';
import { card, compass, toast, type Tone } from './notify';
import { connected, flatDistance, onStreet, playerById } from './util';
import { events, every, onStop } from './lifecycle';
import { SIGN_ON_RADIUS, type Station, type StationPlace } from './stations';

export interface ShiftEnd {
    /** Why it ended, for the toast; null ends it silently. */
    message: string | null;
    tone: Tone;
    /** False when the player is disconnecting: nothing can be shown to them. */
    online: boolean;
}

export interface ShiftDefinition<S> {
    /** The player's exclusive activity while on duty. */
    kind: life.ActivityKind;
    /** The activity's label, as others see it: "Fire duty". */
    activity: string;
    /** The title of the job's toasts: "Fire brigade". */
    title: string;
    /** The id of the job's HUD card and compass source; both are cleared at the end. */
    hud: string;
    compassPriority: number;
    /** A public player flag set while on duty (nametags, the scoreboard): `fr.job` = "fire". */
    flag?: { key: string; value: unknown };
    /** The model worn on duty; the player's own model and weapons come back at the end. */
    uniform?: string;
    /** The weapons carried on duty (with a uniform; none when omitted). */
    kit?: life.Kit;
    /** Signing on happens here, under the arrow; anywhere else the compass shows the way. */
    station?: {
        station: Station;
        /** Told when there is no such station on this map (asked then, with a function). */
        missing: string | (() => string);
        /** How to find the arrow, when the player asks away from it. */
        directions: (place: StationPlace) => string;
        /** True lets the player stay at the wheel of this station vehicle off duty (a loaded farm truck). */
        keepsWheel?: (vehicle: Vehicle) => boolean;
    };
    /** Ends the shift when another module took the player's activity (the jail did): told this. */
    interrupted?: string;
    /** Told when the mission changes under a shift. */
    missionChange?: string;
    tickMs?: number;

    /** Checks that are the job's own and its state; a string refuses with that message. */
    begin(player: Player): S | string;
    /** After the player is on duty: the welcome, spawned vehicles, feed lines. */
    started?(player: Player, state: S): void;
    /** Every tickMs, before the crew's ticks: the job's own clock (calls coming in, running out). */
    beforeTick?(now: number): void;
    /** Every tickMs, for each player on duty. */
    tick?(player: Player, state: S, now: number): void;
    /** Every tickMs, after the crew's ticks: what is shown from where they all are now. */
    afterTick?(now: number): void;
    /** The job's own teardown; runs before the uniform comes off. */
    end?(player: Player, state: S, how: ShiftEnd): void;
    /** Appended to the end toast: what the shift earned. */
    summary?(state: S): string;
}

export interface Shift<S> {
    has(player: Player): boolean;
    get(player: Player): S | undefined;
    /** Everyone on duty, connected, with their state. */
    crew(): [Player, S][];
    readonly size: number;
    start(player: Player): void;
    stop(player: Player, message?: string | null, tone?: Tone): void;
}

interface Duty<S> {
    state: S;
    model: string | null;
    loadout: life.Loadout | null;
}

export function defineShift<S>(definition: ShiftDefinition<S>): Shift<S> {
    const duties = new Map<number, Duty<S>>();

    function finish(player: Player, how: ShiftEnd): void {
        const duty = duties.get(player.id);

        if (!duty) {
            return;
        }

        duties.delete(player.id);

        // Cleared before the job's own end, which may put up what outlives the
        // shift (a loaded farm truck's card and compass).
        if (how.online) {
            compass(player, definition.hud, definition.compassPriority, null);
            card(player, definition.hud, null);
        }

        definition.end?.(player, duty.state, how);
        life.end(player, definition.kind);

        if (!how.online) {
            return;
        }

        jobkey.release(player);
        jobkey.prompt(player, null);

        if (definition.flag) {
            profiles.setPublic(player, definition.flag.key, null);
        }

        const station = definition.station?.station;
        const vehicle = player.getVehicle();

        // Off duty nobody drives a station's vehicle: the player gets out first.
        if (
            station &&
            vehicle &&
            player.getSeat() === 0 &&
            station.isVehicle(vehicle) &&
            !definition.station?.keepsWheel?.(vehicle)
        ) {
            player.removeFromVehicle();
        }

        if (duty.model !== null) {
            player.setModel(duty.model);
        }

        // Not into a cell: the jail took the player off the job and their guns.
        if (duty.loadout && player.alive && !life.activity(player)) {
            life.equip(player, duty.loadout.weapons, duty.loadout.selected);
        }

        station?.refresh();

        if (how.message) {
            const summary = definition.summary?.(duty.state);

            toast(player, summary ? `${how.message} ${summary}` : how.message, {
                title: definition.title,
                tone: how.tone,
            });
        }
    }

    // Away from the station's arrow, asking again toggles the compass to it.
    function atStation(player: Player): boolean {
        const config = definition.station;

        if (!config) {
            return true;
        }

        const place = config.station.here();

        if (!place) {
            toast(player, typeof config.missing === 'string' ? config.missing : config.missing(), {
                tone: 'bad',
            });

            return false;
        }

        if (
            !player.getVehicle() &&
            flatDistance(player.position, place.signOn) <= SIGN_ON_RADIUS + 1
        ) {
            return true;
        }

        if (config.station.guide(player)) {
            toast(player, `${config.directions(place)} Hold TAB to see it on the map.`, {
                title: definition.title,
                tone: 'gold',
                seconds: 8,
            });
        } else {
            toast(player, 'The compass no longer shows the way.', {
                title: definition.title,
                seconds: 3,
            });
        }

        return false;
    }

    function start(player: Player): void {
        if (duties.has(player.id) || !onStreet(player) || !atStation(player)) {
            return;
        }

        if (!life.begin(player, definition.kind, definition.activity)) {
            toast(player, life.busyMessage(player) ?? 'You are busy.', { tone: 'bad' });

            return;
        }

        const state = definition.begin(player);

        if (typeof state === 'string') {
            life.end(player, definition.kind);
            toast(player, state, { tone: 'bad' });

            return;
        }

        const uniform = definition.uniform;

        duties.set(player.id, {
            state,
            model: uniform ? player.model : null,
            loadout: uniform ? life.loadoutOf(player) : null,
        });

        if (uniform) {
            player.setModel(uniform);
            life.giveKit(player, definition.kit ?? { weapons: [], selected: 0 });
        }

        if (definition.flag) {
            profiles.setPublic(player, definition.flag.key, definition.flag.value);
        }

        definition.started?.(player, state);
    }

    function stop(player: Player, message: string | null = null, tone: Tone = 'info'): void {
        finish(player, { message, tone, online: connected(player) });
    }

    function crew(): [Player, S][] {
        const result: [Player, S][] = [];

        for (const [id, duty] of duties) {
            const player = playerById(id);

            if (player) {
                result.push([player, duty.state]);
            }
        }

        return result;
    }

    every(definition.tickMs ?? 1000, () => {
        const now = Date.now();

        definition.beforeTick?.(now);

        for (const [player, state] of crew()) {
            if (definition.interrupted && life.activity(player)?.kind !== definition.kind) {
                stop(player, definition.interrupted);
            } else {
                definition.tick?.(player, state, now);
            }
        }

        definition.afterTick?.(now);
    });

    events.on('playerDisconnect', (player) =>
        finish(player, { message: null, tone: 'info', online: false }),
    );

    events.on('missionChange', () => {
        for (const [player] of crew()) {
            stop(player, definition.missionChange ?? 'Your shift ended with the city reloading.');
        }
    });

    onStop(() => {
        for (const [player] of crew()) {
            stop(player);
        }
    });

    return {
        has: (player) => duties.has(player.id),
        get: (player) => duties.get(player.id)?.state,
        crew,
        get size() {
            return duties.size;
        },
        start,
        stop,
    };
}
