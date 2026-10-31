// Recording a route: /race create starts where the car stands, /race cp
// drops a checkpoint where it is, /race save puts the route in the book.
import * as maps from '../lib/maps';
import { toast, markers, feed, name, text, COLORS, type Marker } from '../lib/notify';
import {
    flatDistance,
    playable,
    formatMinutes,
    playerById,
    plain,
    headingOf,
    type Placement,
    type Point,
} from '../lib/util';
import { PlayerMap } from '../lib/players';
import { Cooldown } from '../lib/cooldowns';
import { onMissionChange } from '../lib/lifecycle';
import { customRoutes, allRoutes, findRoute, slug, save, deleteRecord, pathLength } from './routes';

const MIN_CUSTOM_LENGTH = 600;
const CREATE_COOLDOWN_MS = 5 * 60_000;
const MIN_CUSTOM_POINTS = 3;
const MAX_CUSTOM_POINTS = 40;
const MIN_CUSTOM_SPACING = 40;
const MAX_CUSTOM_ROUTES = 60;
const MAX_ROUTES_PER_AUTHOR = 5;
const MARKERS_EVERY_MS = 2000;

interface Recording {
    name: string;
    start: Placement;
    checkpoints: Point[];
    /** When the markers were last sent; 0 sends them on the next tick. */
    markersAt: number;
}

export const recordings = new PlayerMap<Recording>({ clearOnMissionChange: true });
const routesCreated = new Cooldown(CREATE_COOLDOWN_MS);

function recordingMarkers(player: Player, recording: Recording): void {
    const items: Marker[] = [
        {
            id: 'start',
            position: recording.start.position,
            label: `START · ${recording.name}`,
            color: COLORS.gold,
            beacon: true,
        },
        ...recording.checkpoints.map((point, index) => ({
            id: `cp${index}`,
            position: point,
            label: `${index + 1}`,
            color: COLORS.blue,
            beacon: true,
        })),
    ];

    markers(player, 'race-edit', items);
}

/** Refreshes each recording player's markers every couple of seconds. */
export function tickRecordings(now: number): void {
    for (const [id, recording] of recordings) {
        const player = playerById(id);

        if (!player) {
            recordings.delete(id);
        } else if (now - recording.markersAt > MARKERS_EVERY_MS) {
            recording.markersAt = now;
            recordingMarkers(player, recording);
        }
    }
}

function driverCar(player: Player): Vehicle | null {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;

    if (!car) {
        toast(player, "Record routes from the driver's seat.", { tone: 'bad' });
    }

    return car;
}

export function create(player: Player, args: string[]): void {
    const routeName = args.join(' ').trim();

    if (!/^[A-Za-z0-9 '.-]{3,28}$/.test(routeName)) {
        toast(player, 'Name the route with 3 to 28 letters, digits or spaces.', { tone: 'bad' });

        return;
    }

    // Recorded route IDs are shared by every map, so names must be too.
    if (
        allRoutes().some((route) => slug(route.name) === slug(routeName)) ||
        customRoutes.some((route) => route.id === slug(routeName))
    ) {
        toast(player, 'A route with that name exists.', { tone: 'bad' });

        return;
    }

    const car = driverCar(player);

    if (!car || !playable(player)) {
        return;
    }

    const wait = routesCreated.remaining(player);

    if (wait > 0) {
        toast(player, `You can record another route in ${formatMinutes(wait)}.`, { tone: 'bad' });

        return;
    }

    recordings.set(player.id, {
        name: routeName,
        start: { position: plain(car.position), heading: headingOf(car.rotation) },
        checkpoints: [],
        markersAt: 0,
    });

    toast(
        player,
        'Recording. Drive the route and type /race cp at each checkpoint, /race save when done.',
        { title: routeName, tone: 'gold', seconds: 8 },
    );
}

export function checkpoint(player: Player): void {
    const recording = recordings.get(player.id);
    const car = recording && driverCar(player);

    if (!recording || !car) {
        return;
    }

    const last =
        recording.checkpoints[recording.checkpoints.length - 1] ?? recording.start.position;

    if (flatDistance(last, car.position) < MIN_CUSTOM_SPACING) {
        toast(player, `Checkpoints need ${MIN_CUSTOM_SPACING} m between them.`, { tone: 'bad' });

        return;
    }

    if (recording.checkpoints.length >= MAX_CUSTOM_POINTS) {
        toast(player, 'That is as many checkpoints as a route can have. /race save it.', {
            tone: 'bad',
        });

        return;
    }

    recording.checkpoints.push(plain(car.position));
    recording.markersAt = 0;
    toast(player, `Checkpoint ${recording.checkpoints.length} placed.`, {
        tone: 'good',
        seconds: 2,
    });
}

export function undo(player: Player): void {
    const recording = recordings.get(player.id);

    if (recording?.checkpoints.pop()) {
        recording.markersAt = 0;
    }
}

export function saveRecording(player: Player): void {
    const recording = recordings.get(player.id);

    if (!recording) {
        return;
    }

    if (recording.checkpoints.length < MIN_CUSTOM_POINTS) {
        toast(player, `A route needs at least ${MIN_CUSTOM_POINTS} checkpoints.`, { tone: 'bad' });

        return;
    }

    const length = pathLength(recording.start.position, recording.checkpoints);

    if (length < MIN_CUSTOM_LENGTH) {
        toast(
            player,
            `A route needs at least ${MIN_CUSTOM_LENGTH} m between its checkpoints; this one is ${Math.round(length)} m.`,
            { tone: 'bad' },
        );

        return;
    }

    const mine = customRoutes.filter((route) => route.author === player.nickname);

    if (mine.length >= MAX_ROUTES_PER_AUTHOR || customRoutes.length >= MAX_CUSTOM_ROUTES) {
        toast(player, 'The route book is full. Delete one of yours with /race delete <name>.', {
            tone: 'bad',
        });

        return;
    }

    customRoutes.push({
        id: slug(recording.name),
        name: recording.name,
        author: player.nickname,
        start: recording.start,
        checkpoints: recording.checkpoints,
        mission: maps.current()?.id ?? 'freeride',
        created: Date.now(),
    });

    recordings.delete(player.id);
    routesCreated.start(player);
    markers(player, 'race-edit', []);
    save();
    toast(
        player,
        `${recording.name} is in the route book. Open it with /race start ${recording.name}.`,
        { tone: 'good' },
    );

    feed('race', [
        name(player, COLORS.gold),
        text(' published a new route: '),
        text(recording.name, COLORS.white),
    ]);
}

export function discard(player: Player): void {
    if (recordings.delete(player.id)) {
        markers(player, 'race-edit', []);
    }
}

export function deleteRoute(player: Player, args: string[]): void {
    const route = findRoute(args.join(' '));
    const index =
        route && !route.builtIn
            ? customRoutes.findIndex((entry) => `c:${entry.id}` === route.id)
            : -1;

    if (!route || index < 0 || customRoutes[index].author !== player.nickname) {
        toast(player, 'You can delete only routes you recorded.', { tone: 'bad' });

        return;
    }

    customRoutes.splice(index, 1);
    deleteRecord(route.id);
    save();
    toast(player, `${route.name} removed from the route book.`);
}

onMissionChange(() => {
    for (const player of World.getPlayers()) {
        markers(player, 'race-edit', []);
    }
});
