// Putting a logged-in player on the street: where they left off, or at their
// usual district.
//
// Each map's districts are listed in lib/maps.ts. Most are the mission's own
// emeth_* frames: the client reports them and the server checks each against
// that map's stock position, so no client can choose its own spawn point.
// Districts without a frame in the loaded scene (Lost Heaven Extended's
// countryside) are placed by the server alone. Each waiting player is held
// in a private virtual world so another player's ped never walks into their
// preview. The server keeps a returning player's last street position,
// health, weapons and wanted level in their record.
import * as profiles from '../lib/profiles';
import * as life from '../lib/life';
import * as maps from '../lib/maps';
import * as log from '../lib/log';
import { MAIN_WORLD, inFreeRide, distance, plain, headingOf, emit, type Point } from '../lib/util';
import { PlayerMap } from '../lib/players';
import { fields, isFiniteNumber } from '../lib/payload';
import { CHARACTERS } from './characters';
import { sessions } from './sessions';

/** A district's spot: where to stand and which way to face. */
export interface Anchor {
    position: Point;
    direction: Point;
}

/** The districts a waiting player's client found on the loaded map. */
interface Selection {
    generation: number;
    /** By district id. */
    anchors: Map<string, Anchor>;
    /** Where the player comes back after a death. */
    choice: { locationId: string } | null;
}

const FIRST_PREVIEW_WORLD = 0x40000000;
const MAX_ANCHOR_DELTA = 3;
// Closer than this to the saved spot it is not saved again.
const RESUME_MOVED = 5;

const selections = new PlayerMap<Selection>({ clearOnMissionChange: true });
const previewWorlds = new PlayerMap<number>();
let nextPreviewWorld = FIRST_PREVIEW_WORLD;

export function isolatePreview(player: Player): void {
    if (!inFreeRide() || player.spawned) {
        return;
    }

    let world = previewWorlds.get(player.id);

    if (world === undefined) {
        world = nextPreviewWorld++;
        previewWorlds.set(player.id, world);
    }

    if (player.virtualWorld !== world) {
        player.setVirtualWorld(world);
    }
}

export function isolateWaitingPlayers(): void {
    for (const player of World.getPlayers()) {
        isolatePreview(player);
    }
}

export function leavePreviewWorlds(): void {
    for (const player of World.getPlayers()) {
        if (previewWorlds.has(player.id)) {
            player.setVirtualWorld(MAIN_WORLD);
        }
    }

    previewWorlds.clear();
}

export function clearSelections(): void {
    selections.clear();
}

function point(value: unknown): Point | null {
    const { x, y, z } = fields(value);

    return isFiniteNumber(x) && isFiniteNumber(y) && isFiniteNumber(z) ? { x, y, z } : null;
}

// The client supplies the facing; the position must match the stock frame.
function validAnchor(report: unknown, frame: string): Anchor | null {
    const stock = maps.current()?.frames[frame];
    const reported = fields(report);

    if (!stock || reported.frame !== frame) {
        return null;
    }

    const position = point(reported.position);
    const direction = point(reported.direction);

    if (
        !position ||
        !direction ||
        Math.hypot(direction.x, direction.z) < 0.25 ||
        distance(position, stock) > MAX_ANCHOR_DELTA
    ) {
        return null;
    }

    return { position: stock, direction };
}

function anchorHeading(anchor: Anchor): number {
    return Math.atan2(anchor.direction.x, anchor.direction.z);
}

/**
 * Takes the districts a waiting player's client reported for the loaded map
 * (its hello); false when they do not make a choice (fewer than two).
 */
export function takeAnchors(
    player: Player,
    generation: number,
    reports: readonly unknown[],
): boolean {
    const map = maps.current();

    if (!map) {
        return false;
    }

    const anchors = new Map<string, Anchor>();

    for (const district of map.districts) {
        const { frame } = district;
        const anchor =
            frame === undefined
                ? { position: district.position, direction: district.direction }
                : validAnchor(
                      reports.find((candidate) => fields(candidate).frame === frame),
                      frame,
                  );

        if (anchor) {
            anchors.set(district.id, anchor);
        }
    }

    if (anchors.size < 2) {
        return false;
    }

    const previous = selections.get(player.id);
    const selection =
        previous && previous.generation === generation
            ? previous
            : { generation, anchors, choice: null };

    selection.anchors = anchors;
    selections.set(player.id, selection);

    return true;
}

// The last place a player stood on this map's streets, saved while they
// play. A race, a duel, a job or a cell is not a place to come back to, so
// the previous spot is kept through them.
export function saveResume(player: Player): void {
    if (
        !inFreeRide() ||
        !player.spawned ||
        !player.alive ||
        player.missionGeneration !== World.getMissionGeneration() ||
        player.virtualWorld !== MAIN_WORLD ||
        life.activity(player)
    ) {
        return;
    }

    const position = plain(player.position);

    if (![position.x, position.y, position.z].every(Number.isFinite)) {
        return;
    }

    const { weapons, selected } = life.loadoutOf(player);
    const resume: profiles.Resume = {
        mission: World.getMission(),
        position,
        heading: headingOf(player.rotation),
        health: player.health,
        weapons: [...weapons],
        selected,
        wanted: Police.getRecord(player)?.level ?? 0,
        at: Date.now(),
    };
    // Every change rewrites the whole record: a player standing still, or
    // barely moving, keeps the one already saved.
    const saved = profiles.get(player)?.resume;

    if (
        saved &&
        saved.mission === resume.mission &&
        distance(saved.position, position) < RESUME_MOVED &&
        saved.health === resume.health &&
        saved.selected === resume.selected &&
        saved.wanted === resume.wanted &&
        JSON.stringify(saved.weapons) === JSON.stringify(weapons)
    ) {
        return;
    }

    profiles.setField(player, 'resume', resume);
}

function resumeOf(player: Player): profiles.Resume | null {
    const record = profiles.get(player);
    const resume = record?.resume;

    if (!resume || resume.mission !== World.getMission() || (record.jailUntil ?? 0) > Date.now()) {
        return null;
    }

    return resume;
}

function nearestDistrict(selection: Selection, position: Point): maps.District | null {
    let best: { district: maps.District; anchor: Anchor } | null = null;

    for (const district of maps.current()?.districts ?? []) {
        const anchor = selection.anchors.get(district.id);

        if (
            anchor &&
            (!best ||
                distance(anchor.position, position) < distance(best.anchor.position, position))
        ) {
            best = { district, anchor };
        }
    }

    return best?.district ?? null;
}

// The player's usual district on this map, else the map's first one.
function homeDistrict(player: Player, selection: Selection): maps.District | null {
    const districts = maps.current()?.districts ?? [];
    const usual = profiles.get(player)?.district;

    return (
        districts.find((district) => district.id === usual && selection.anchors.has(district.id)) ??
        districts.find((district) => selection.anchors.has(district.id)) ??
        null
    );
}

function currentSelection(player: Player): Selection | null {
    const selection = selections.get(player.id);

    return selection && selection.generation === World.getMissionGeneration() ? selection : null;
}

/** Puts a logged-in player whose look is chosen on the street; false when they cannot be yet. */
export function enter(player: Player): boolean {
    const session = sessions.get(player.id);
    const selection = currentSelection(player);

    if (
        !inFreeRide() ||
        !World.isReady() ||
        player.spawned ||
        !session ||
        session.needsLook ||
        !selection
    ) {
        return false;
    }

    const record = profiles.get(player);
    const resume = resumeOf(player);
    const home = homeDistrict(player, selection);
    const anchor = resume
        ? {
              position: resume.position,
              direction: { x: Math.sin(resume.heading), y: 0, z: Math.cos(resume.heading) },
          }
        : home && selection.anchors.get(home.id);

    if (!anchor || !record) {
        return false;
    }

    // The saved look; a record whose file the server rejects falls back to
    // the first stock character.
    if (!record.model || !player.setModel(record.model)) {
        record.model = CHARACTERS[0].file;
        player.setModel(record.model);
        profiles.setField(player, 'model', record.model);
    }

    if (!player.spawn(anchor.position, anchorHeading(anchor))) {
        emit(player, 'fr:login:error', {
            message: 'That entrance is unavailable. Try again in a moment.',
        });

        return false;
    }

    player.setVirtualWorld(MAIN_WORLD);
    previewWorlds.delete(player.id);
    // Dead players come back at their usual district, not the resume spot.
    selection.choice = home ? { locationId: home.id } : null;

    if (home && !record.district) {
        profiles.setField(player, 'district', home.id);
    }

    if (resume) {
        // The spawn handlers gave the starting kit; this life carries on the last one.
        life.equip(player, resume.weapons ?? [], resume.selected);

        if (resume.health > 0 && resume.health < 100) {
            player.setHealth(resume.health);
        }

        if (resume.wanted > 0) {
            Police.setWanted(player, resume.wanted);
        }
    }

    const near = resume && nearestDistrict(selection, resume.position);
    const where = resume
        ? `where they left off${near ? ` near ${near.name}` : ''}`
        : (home?.name ?? '');

    emit(player, 'fr:login:accepted', { where });
    log.info(`${player.nickname} entered ${where}`);

    return true;
}

/** The district the one-time look is chosen at, where the player will start. */
export function lookAnchor(player: Player): Anchor | null {
    const selection = currentSelection(player);
    const home = selection && homeDistrict(player, selection);
    const anchor = home && selection.anchors.get(home.id);

    return anchor ? { position: anchor.position, direction: anchor.direction } : null;
}

// Dead players come back at their usual district; without one (another
// mission, or after a resource restart) lib/life.ts falls back to the native
// spawn hint.
life.setRespawnProvider((player) => {
    if (!inFreeRide()) {
        return null;
    }

    const selection = currentSelection(player);
    const anchor = selection?.choice ? selection.anchors.get(selection.choice.locationId) : null;

    return anchor ? { position: anchor.position, heading: anchorHeading(anchor) } : null;
});
