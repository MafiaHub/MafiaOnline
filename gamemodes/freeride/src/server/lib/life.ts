// Who decides where a player comes back, what they carry, and what they are
// busy with. Death never respawns anyone by itself; this module does it after
// a short delay, asking the active override (a duel) or the default provider
// (the district the player chose) for the place.
import * as log from './log';
import * as profiles from './profiles';
import { events } from './lifecycle';
import { PlayerMap, PlayerSet } from './players';
import type { Placement, Point } from './util';

export const RESPAWN_DELAY_MS = 5000;

/** A weapon handed out: the native fills in the rest of the slot. */
export interface WeaponGrant {
    weaponId: number;
    loaded?: number;
    reserve?: number;
}

export interface Kit {
    weapons: readonly WeaponGrant[];
    selected: number;
}

/** What a player carried and how healthy they were, to give back later. */
export interface Loadout extends Kit {
    health: number;
}

/** A bat; guns are bought at Yellow Pete's. */
export const STARTER_KIT: Kit = Object.freeze({ weapons: [{ weaponId: 4 }], selected: 4 });

/** The exclusive things a player can be busy with. */
export type ActivityKind =
    | 'bootleg'
    | 'courier'
    | 'duel'
    | 'duty'
    | 'farm'
    | 'fire'
    | 'jail'
    | 'medic'
    | 'race'
    | 'race-lobby'
    | 'robbery'
    | 'taxi'
    | 'visit';

export interface Activity {
    kind: ActivityKind;
    label: string;
}

/** How a player's lives go while something (a duel, a shift) is in charge of them. */
export interface LifeOverride {
    spawn?: (player: Player) => Placement | null;
    kit?: Kit;
    delayMs?: number;
}

type SpotProvider = (player: Player) => Placement | null;

const activities = new PlayerMap<Activity>();
const overrides = new PlayerMap<LifeOverride>({ clearOnMissionChange: true });
// The spawn generation of a life that is a move, not a new life.
const transit = new PlayerMap<number>({ clearOnMissionChange: true });
const pending = new PlayerSet();
// Deaths whose respawn waits, until a time: a medic is on the way.
const holds = new PlayerMap<number>({ clearOnMissionChange: true });
let provider: SpotProvider = () => null;
let hospital: () => Placement | null = () => null;

/** One exclusive activity per player: a race, a duel, a delivery run. False when busy with another. */
export function begin(player: Player, kind: ActivityKind, label: string): boolean {
    const current = activities.get(player.id);

    if (current && current.kind !== kind) {
        return false;
    }

    activities.set(player.id, { kind, label });
    profiles.setPublic(player, 'fr.activity', label);

    return true;
}

/** Ends the player's activity; with `kind`, only if it is that one. */
export function end(player: Player, kind?: ActivityKind): void {
    const current = activities.get(player.id);

    if (!current || (kind && current.kind !== kind)) {
        return;
    }

    activities.delete(player.id);
    profiles.setPublic(player, 'fr.activity', null);
}

export function activity(player: Player): Activity | null {
    return activities.get(player.id) ?? null;
}

/** Why the player cannot start something else, or null when they are free. */
export function busyMessage(player: Player): string | null {
    const current = activities.get(player.id);

    return current ? `Finish your ${current.label.toLowerCase()} first.` : null;
}

export function setRespawnProvider(handler: SpotProvider): void {
    provider = handler;
}

export function setHospital(handler: () => Placement | null): void {
    hospital = handler;
}

export function setOverride(player: Player, override: LifeOverride): void {
    overrides.set(player.id, override);
}

export function clearOverride(player: Player): void {
    overrides.delete(player.id);
}

// setInventory takes a selected weapon only when one is given; an explicit
// undefined is rejected.
export function equip(player: Player, weapons: readonly WeaponGrant[], selected?: number): boolean {
    return selected
        ? player.setInventory([...weapons], selected)
        : player.setInventory([...weapons]);
}

export function giveKit(player: Player, kit: Kit = STARTER_KIT): void {
    if (!equip(player, kit.weapons, kit.selected)) {
        log.error(`Could not equip ${player.nickname}`);
    }
}

export function loadoutOf(player: Player): Loadout {
    const inventory = player.getInventory();

    return {
        weapons: (inventory?.weapons ?? []).map(({ weaponId, loaded, reserve }) => ({
            weaponId,
            loaded,
            reserve,
        })),
        selected: inventory?.selected ?? 0,
        health: player.health,
    };
}

/**
 * A move built on spawn that keeps a loadout: by default the current life's
 * weapons and health. The new life is marked so no starting kit replaces it.
 * Spawn events run inside player.spawn(), so the life is marked pending
 * before the call and the spawn handlers already see it as a transit.
 */
export function teleport(
    player: Player,
    position: Point,
    heading = 0,
    loadout: Loadout = loadoutOf(player),
): boolean {
    pending.add(player.id);
    const spawned = player.spawn(position, heading);

    pending.delete(player.id);

    if (!spawned) {
        return false;
    }

    transit.set(player.id, player.spawnGeneration);
    equip(player, loadout.weapons, loadout.selected);

    if (loadout.health < 100) {
        player.setHealth(Math.max(1, loadout.health));
    }

    return true;
}

/** Where a player comes back: the active override, the map's hospital, the district they chose, or the native spawn hint. */
export function spotFor(player: Player): Placement | null {
    const suggested = player.getSuggestedSpawn();

    return (
        overrides.get(player.id)?.spawn?.(player) ??
        hospital() ??
        provider(player) ??
        (suggested ? { position: suggested, heading: 0 } : null)
    );
}

/** A fresh life at the player's usual place, as after an arrest. */
export function restart(player: Player): boolean {
    const spot = spotFor(player);

    return Boolean(spot) && player.spawn(spot!.position, spot!.heading ?? 0);
}

export function isTransit(player: Player): boolean {
    return pending.has(player.id) || transit.get(player.id) === player.spawnGeneration;
}

function spawnKit(player: Player): void {
    if (!isTransit(player)) {
        giveKit(player, overrides.get(player.id)?.kit ?? STARTER_KIT);
    }
}

events.on('playerSpawn', spawnKit);
events.on('playerRespawn', spawnKit);

events.on('playerDeath', (player, _killer, { missionGeneration, spawnGeneration }) => {
    const delay = overrides.get(player.id)?.delayMs ?? RESPAWN_DELAY_MS;
    // Respawn fails while any client is still loading the mission; keep
    // trying for as long as this life is the one that died.
    const tryRespawn = (): void => {
        if (
            player.alive ||
            player.spawnGeneration !== spawnGeneration ||
            World.getMissionGeneration() !== missionGeneration ||
            !World.getPlayers().some((other) => other.id === player.id)
        ) {
            return;
        }

        if ((holds.get(player.id) ?? 0) > Date.now()) {
            setTimeout(tryRespawn, 1000);

            return;
        }

        const spot = World.isReady() ? spotFor(player) : null;

        if (spot && player.respawn(spot.position, spot.heading ?? 0)) {
            return;
        }

        setTimeout(tryRespawn, 1000);
    };

    setTimeout(tryRespawn, delay);
});

/** Keeps a dead player down until `until` (a Date.now() time), or lets the usual respawn go ahead again with null. */
export function holdRespawn(player: Player, until: number | null): void {
    if (until) {
        holds.set(player.id, until);
    } else {
        holds.delete(player.id);
    }
}

/** A dead player brought back where they lie, as a medic does: a transit life (no hospital bill), the starting kit and the given health. */
export function revive(player: Player, position: Point, heading = 0, health = 60): boolean {
    if (player.alive) {
        return false;
    }

    holds.delete(player.id);
    pending.add(player.id);
    const revived = player.respawn(position, heading);

    pending.delete(player.id);

    if (!revived) {
        return false;
    }

    transit.set(player.id, player.spawnGeneration);
    giveKit(player, overrides.get(player.id)?.kit ?? STARTER_KIT);

    if (health < 100) {
        player.setHealth(Math.max(1, health));
    }

    return true;
}

events.on('playerSpawn', (player) => holds.delete(player.id));
events.on('playerRespawn', (player) => holds.delete(player.id));
