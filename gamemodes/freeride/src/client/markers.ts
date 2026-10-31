// Markers drawn by this client: race checkpoints, event beacons and crates,
// door arrows, turf rings, job stations, bounty labels over wanted players,
// and blips on the city map (hold TAB) for those and for crewmates. Groups
// come from the server; nothing drawn here can claim a reward. An item may
// carry `range` (hidden farther away), `ring` (a ground circle of that
// radius), `beacon`, `model`, `primary`, `near` (how close its label shows),
// `hint` (a second label line shown up close) and `map`: false keeps it off
// the city map, an object sets its blip
// ({icon, label, size, edge, flash}). Without `map`, objectives, beacons and
// markers seen from afar get a plain blip; the close-up ones stay off.
import { argb, rgb } from './lib/color';
import { formatMoney } from './lib/format';
import { flatDistance, type Point } from './lib/geometry';
import { events, onServer, onStop } from './lib/runtime';

const BEACON_HEIGHT = 40;
const BEACON_RANGE = 3000;
// World labels show only near and in sight; the city map covers the rest.
const LABEL_NEAR = 60;
const PRIMARY_LABEL_NEAR = 250;
const MAP_FROM_RANGE = 150;
const BOUNTY_RANGE = 350;
const MODEL_RETRY_MS = 2000;
const BLIP_SYNC_MS = 500;
const RING_SEGMENTS = 32;
// A ring this far away is a few pixels across: fewer segments do.
const FAR_RING_SEGMENTS = 12;
const FAR_RING_DISTANCE = 50;
// How often a label's line of sight (a raycast) and the bounty list are
// checked; the labels still follow their positions every frame.
const LABEL_SIGHT_MS = 150;
const BOUNTY_REFRESH_MS = 250;
const HINT_NEAR = 15;
const ARROW_MODEL = 'sipka.i3d';
const FALLBACK_MODEL = '9money.i3d';

type BlipOptions = NonNullable<Parameters<typeof CityMap.setBlip>[2]>;
type BlipTarget = Point | Player | Vehicle;

interface MarkerItem {
    id: string | number;
    /** Where it is; for a vehicle item, roughly, for while the vehicle is not streamed here. */
    position?: Point | null;
    vehicleId?: number;
    label?: string;
    /** '#rrggbb'. */
    color?: string;
    range?: number;
    ring?: number;
    beacon?: boolean;
    model?: string;
    primary?: boolean;
    near?: number;
    hint?: string;
    map?: false | Pick<BlipOptions, 'icon' | 'label' | 'size' | 'edge' | 'flash'> | null;
}

interface MarkerGroup {
    group: string;
    items: MarkerItem[];
}

interface ModelRecord {
    frame: Frame | null;
    nextAttempt: number;
    model: string;
}

const groups = new Map<string, MarkerItem[]>();
// Group/item key -> the model drawn for it.
const models = new Map<string, ModelRecord>();
// Blip id -> the options it was last set with, so only changes are sent.
const blips = new Map<string, string>();
let blipsSyncedAt = 0;
// This frame's streamed vehicles by id, built on first use: World.getVehicles()
// makes a new object for every vehicle, once per frame is enough.
let frameVehicles: Map<number, Vehicle> | null = null;
// Item key -> its label's last line-of-sight test.
const labelSight = new Map<string, { at: number; visible: boolean }>();
// Wanted players, refreshed every BOUNTY_REFRESH_MS.
let bounties: { player: Player; text: string }[] = [];
let bountiesAt = 0;

function itemKey(group: string, item: MarkerItem): string {
    return `${group}/${item.id}`;
}

function streamedVehicle(item: MarkerItem): Vehicle | null {
    if (typeof item.vehicleId !== 'number') {
        return null;
    }

    frameVehicles ??= new Map(World.getVehicles().map((vehicle) => [vehicle.id, vehicle]));

    return frameVehicles.get(item.vehicleId) ?? null;
}

// A vehicle item follows the vehicle while it is streamed here, and falls
// back to the rough position the server sent with it otherwise.
function itemPosition(item: MarkerItem): Point | null {
    return streamedVehicle(item)?.getWorldTransform()?.position ?? item.position ?? null;
}

/** Where a world point is on the screen; null when it is not in front of the camera. */
function screenPoint(position: Point): { x: number; y: number } | null {
    const point = Scene.projectWorld(position, false);

    return point && point.x !== null && point.y !== null ? { x: point.x, y: point.y } : null;
}

function releaseModels(group: string | null): void {
    for (const [key, record] of models) {
        if (group && !key.startsWith(`${group}/`)) {
            continue;
        }

        record.frame?.destroy();
        models.delete(key);
    }
}

function drawBeacon(base: Point, color: string | undefined, distance: number): void {
    const bottom = screenPoint(base);
    const top = screenPoint({ x: base.x, y: base.y + BEACON_HEIGHT, z: base.z });

    if (!bottom || !top) {
        return;
    }

    const width = Math.max(2, Math.min(10, 10 - distance / 120));

    Draw.line(bottom.x, bottom.y, top.x, top.y, width + 6, argb(color, 0.12));
    Draw.line(bottom.x, bottom.y, top.x, top.y, width, argb(color, 0.55));
}

function hideModel(group: string, item: MarkerItem): void {
    models.get(itemKey(group, item))?.frame?.setVisible(false);
}

function circle(
    center: Point,
    radius: number,
    height: number,
    thickness: number,
    color: number,
    segments: number,
): void {
    let previous: { x: number; y: number } | null = null;

    for (let step = 0; step <= segments; step++) {
        const angle = (step / segments) * Math.PI * 2;
        const point = screenPoint({
            x: center.x + Math.cos(angle) * radius,
            y: center.y + height,
            z: center.z + Math.sin(angle) * radius,
        });

        if (point && previous) {
            Draw.line(previous.x, previous.y, point.x, point.y, thickness, color);
        }

        previous = point;
    }
}

function drawRing(
    center: Point,
    radius: number,
    color: string | undefined,
    distance: number,
): void {
    circle(
        center,
        radius,
        0.4,
        3,
        argb(color, 0.6),
        distance - radius > FAR_RING_DISTANCE ? FAR_RING_SEGMENTS : RING_SEGMENTS,
    );
}

// A dark plate centred on (x, y) with the item's colour down its edge: the
// title, and a second line under it when there is one.
function drawPlate(
    x: number,
    y: number,
    title: string,
    subtitle: string | null,
    color: string | undefined,
    alpha: number,
    size: number,
): void {
    const width =
        Math.max(title.length * size * 0.56, subtitle ? subtitle.length * 12 * 0.56 : 0) + 28;
    const height = size + 14 + (subtitle ? 16 : 0);
    const left = x - width / 2;
    const top = y - height / 2;

    Draw.rect(left, top, width, height, argb('#101418', 0.72 * alpha));
    Draw.rect(left, top, 4, height, argb(color, alpha));
    Draw.rect(left + 4, top, width - 4, 1, argb(color, 0.45 * alpha));
    Draw.text(title, x + 2, top + 6 + size, size, argb(color, alpha), 3);

    if (subtitle) {
        Draw.text(subtitle, x + 2, top + size + 22, 12, argb('#f3e9d6', 0.85 * alpha), 3);
    }
}

function drawModel(key: string, model: string, position: Point, now: number): void {
    let record = models.get(key);

    if (!record) {
        record = { frame: null, nextAttempt: 0, model };
        models.set(key, record);
    }

    if (record.frame === null) {
        if (now < record.nextAttempt) {
            return;
        }

        record.nextAttempt = now + MODEL_RETRY_MS;
        record.frame = Scene.createModelFrame(model) ?? Scene.createModelFrame(FALLBACK_MODEL);

        if (record.frame === null) {
            return;
        }
    }

    record.frame.setVisible(true);

    const spin = now * 0.001;
    const bob = 0.12 * Math.sin(now * 0.003);

    if (record.model === ARROW_MODEL) {
        // sipka.4ds is a flat arrow standing in its x-y plane with the tip
        // 1.13 m out along +x. LS3D turns a frame by the conjugate of the
        // stored quaternion, so this stores conj(spin about y * quarter turn
        // about z), which tips it point down and spins it about the vertical,
        // as retail mission arrows do. Its tip then hovers 0.3 m up.
        const c = Math.cos(spin / 2) * Math.SQRT1_2;
        const s = Math.sin(spin / 2) * Math.SQRT1_2;

        record.frame.setWorldPosition({ x: position.x, y: position.y + 1.45 + bob, z: position.z });
        record.frame.setRotation({ w: c, x: s, y: -s, z: c });

        return;
    }

    record.frame.setWorldPosition({ x: position.x, y: position.y + 0.25 + bob, z: position.z });
    record.frame.setRotation({ w: Math.cos(spin / 2), x: 0, y: Math.sin(spin / 2), z: 0 });
}

function drawItems(eye: Point, now: number): void {
    for (const [group, items] of groups) {
        for (const item of items) {
            const position = itemPosition(item);

            if (!position) {
                continue;
            }

            const distance = flatDistance(eye, position);

            if (item.range && distance > item.range) {
                if (item.model) {
                    hideModel(group, item);
                }

                continue;
            }

            const key = itemKey(group, item);

            if (item.model) {
                drawModel(key, item.model, position, now);
            }

            if (item.ring && distance < item.ring * 6) {
                drawRing(position, item.ring, item.color, distance);
            }

            if (item.beacon && distance < BEACON_RANGE) {
                drawBeacon(position, item.color, distance);
            }

            const near = item.near ?? (item.primary ? PRIMARY_LABEL_NEAR : LABEL_NEAR);

            if (!item.label || distance > near) {
                continue;
            }

            // Not through walls: a label behind a building waits for the corner.
            // It sinks toward eye level up close, so it stays in view there.
            const lift = Math.min(
                item.vehicleId ? 2.6 : item.model ? 3.4 : 3,
                1.6 + distance * 0.25,
            );
            const anchor = { x: position.x, y: position.y + lift, z: position.z };
            let sight = labelSight.get(key);

            if (!sight || now - sight.at >= LABEL_SIGHT_MS) {
                sight = { at: now, visible: Boolean(Scene.projectWorld(anchor, true)?.visible) };
                labelSight.set(key, sight);
            }

            if (!sight.visible) {
                continue;
            }

            const label = Scene.projectWorld(anchor, false);

            if (!label?.visible || label.x === null || label.y === null) {
                continue;
            }

            // Fades in over the last fifth of its reach instead of popping up.
            const alpha = Math.min(1, (near - distance) / (near * 0.2));
            const subtitle =
                item.hint && distance < HINT_NEAR
                    ? item.hint
                    : item.primary
                      ? `${Math.round(distance)} m`
                      : null;

            drawPlate(
                label.x,
                label.y,
                item.label,
                subtitle,
                item.color,
                alpha,
                item.primary ? 17 : 14,
            );
        }
    }
}

function drawPeople(eye: Point, now: number): void {
    // Bounties change seldom; reading every player's state each frame does not pay.
    if (now - bountiesAt >= BOUNTY_REFRESH_MS) {
        bountiesAt = now;
        bounties = [];

        for (const player of World.getPlayers()) {
            if (player.isLocal || !player.alive) {
                continue;
            }

            const bounty: unknown = player.state.get('fr.bounty');

            if (typeof bounty === 'number' && bounty) {
                bounties.push({ player, text: `${formatMoney(bounty)} BOUNTY` });
            }
        }
    }

    for (const { player, text } of bounties) {
        // A handle kept from the last refresh stops resolving once its player leaves.
        let position: Point | null = null;

        try {
            position = player.getWorldPosition();
        } catch {
            continue;
        }

        if (!position || flatDistance(eye, position) >= BOUNTY_RANGE) {
            continue;
        }

        Draw.worldText(
            text,
            { x: position.x, y: position.y + 2.7, z: position.z },
            15,
            argb('#d0574a'),
            3,
            true,
        );
    }
}

function titleCase(label: string | undefined): string {
    return (label ?? '')
        .split(' · ')[0]
        .toLowerCase()
        .replace(/(^|\s)\S/g, (letter) => letter.toUpperCase());
}

function blipOf(item: MarkerItem): BlipOptions | null {
    if (item.map === false) {
        return null;
    }

    const spec = typeof item.map === 'object' && item.map ? item.map : null;

    if (!spec && !item.primary && !item.beacon && item.range && item.range < MAP_FROM_RANGE) {
        return null;
    }

    const options: BlipOptions = {
        color: rgb(item.color),
        icon: spec?.icon ?? (item.primary ? 'diamond' : 'dot'),
        size: spec?.size ?? (item.primary ? 6 : 4),
        edge: spec?.edge ?? Boolean(item.primary),
        flash: Boolean(spec?.flash),
    };
    const label = spec?.label ?? (item.primary ? titleCase(item.label) : null);

    if (label) {
        options.label = label;
    }

    return options;
}

/** The crew a player is in (the "fr.crew" state key), if any. */
function crewOf(player: Player): { tag: string; color?: string } | null {
    const crew: unknown = player.state.get('fr.crew');

    return typeof crew === 'object' && crew !== null
        ? (crew as { tag: string; color?: string })
        : null;
}

// What the city map should show: every group's items, then crewmates.
function wantedBlips(): Map<string, { target: BlipTarget; options: BlipOptions }> {
    const wanted = new Map<string, { target: BlipTarget; options: BlipOptions }>();

    for (const [group, items] of groups) {
        for (const item of items) {
            const options = blipOf(item);

            if (!options) {
                continue;
            }

            const target = streamedVehicle(item) ?? item.position ?? null;

            if (target) {
                wanted.set(itemKey(group, item), { target, options });
            }
        }
    }

    const ownCrew = LocalPlayer ? (crewOf(LocalPlayer)?.tag ?? null) : null;

    if (ownCrew) {
        for (const player of World.getPlayers()) {
            const crew = crewOf(player);

            if (player.isLocal || crew?.tag !== ownCrew) {
                continue;
            }

            wanted.set(`crew/${player.id}`, {
                target: player,
                options: { color: rgb(crew.color), icon: 'dot', size: 4, label: player.nickname },
            });
        }
    }

    return wanted;
}

// A player or vehicle's network id; a plain point has none.
function entityId(target: BlipTarget): number | null {
    const id = (target as { id?: unknown }).id;

    return typeof id === 'number' ? id : null;
}

function syncBlips(): void {
    const wanted = wantedBlips();

    for (const id of [...blips.keys()]) {
        if (wanted.has(id)) {
            continue;
        }

        CityMap.removeBlip(id);
        blips.delete(id);
    }

    for (const [id, { target, options }] of wanted) {
        // An entity blip follows it natively; a point is resent when it moves.
        const entity = entityId(target);
        const key = JSON.stringify([options, entity !== null ? `#${entity}` : target]);

        if (blips.get(id) === key) {
            continue;
        }

        if (CityMap.setBlip(id, target, options)) {
            blips.set(id, key);
        }
    }
}

onServer<MarkerGroup>('fr:markers', (request) => {
    if (typeof request.group !== 'string' || !Array.isArray(request.items)) {
        return;
    }

    releaseModels(request.group);

    for (const key of [...labelSight.keys()]) {
        if (key.startsWith(`${request.group}/`)) {
            labelSight.delete(key);
        }
    }

    if (request.items.length) {
        groups.set(request.group, request.items);
    } else {
        groups.delete(request.group);
    }

    blipsSyncedAt = 0;
});

events.on('missionUnload', () => {
    groups.clear();
    models.clear();
    labelSight.clear();
    bounties = [];
    // The mission's blips went with it.
    blips.clear();
});

onStop(() => {
    releaseModels(null);
    groups.clear();
});

events.on('render', () => {
    if (!World.isReady() || !LocalPlayer?.spawned) {
        return;
    }

    const now = Date.now();

    frameVehicles = null;

    // Vehicles and crewmates stream in and out.
    if (now - blipsSyncedAt >= BLIP_SYNC_MS) {
        blipsSyncedAt = now;
        syncBlips();
    }

    // The map covers the screen; nothing in the world shows over it.
    if (CityMap.isOpen()) {
        for (const [group, items] of groups) {
            for (const item of items) {
                if (item.model) {
                    hideModel(group, item);
                }
            }
        }

        return;
    }

    const eye = LocalPlayer.getWorldPosition();

    if (!eye) {
        return;
    }

    drawItems(eye, now);
    drawPeople(eye, now);
});
