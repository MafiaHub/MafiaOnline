// A fire in a bar: the crew goes in through the door, hoses the room and
// leads the barman and the drinkers out to the street (the medics take them
// from there).
import * as barRoom from '../lib/bar_room';
import * as bars from '../bars';
import * as maps from '../lib/maps';
import { CROUCHED } from '../lib/civilians';
import { npcsAvailable } from '../lib/npcs';
import { COLORS, feed, text, toast, toastAll } from '../lib/notify';
import { MAIN_WORLD, plain, type Point } from '../lib/util';
import { crews } from './duty';
import { presentIncidents } from './markers';
import { incidents, newIncident, type BarEntry, type Incident } from './model';
import { trap } from './rescue';

/** Where a fire in one of the bars (server/bars/) burns and who is caught in it. */
interface BarLayout {
    /**
     * The bar's interior in lib/maps.ts, for a bar entered by its arrow; a bar
     * with working doors has none and is walked into.
     */
    interior?: string;
    /** Flames out of the shop windows beside the door. */
    front: Point;
    /** Who is caught inside: [barman, ...drinkers] indices. */
    people: readonly number[];
    /** The fires in the room. */
    rooms: () => (Point & { radius: number })[];
    /** Where those led out stand on the pavement. */
    street: readonly Point[];
}

// Fires in a bar (server/bars/). Morello's bar on Lost Heaven Extended
// (models/morellobar.4ds), walked into by its doors at the sides: flames out
// of the shop windows on the south front (z 451.5), and two in its room
// (floor y 17.3, x -415.7..-406.0, z 451.6..461.2): between the stools and
// the way in, and behind the counter's east end. Stock Lost
// Heaven's back-room bar (lib/bar_room.ts, room-local coordinates): flames
// out of Salieri's west shop window beside his door, one in the aisle in
// front of the counter and one at the far end. The people caught inside are
// the bar's own barman and drinkers, at their own places ([barman,
// ...drinkers] indices). Led out of Morello's by a door and clear of the
// building they are safe where they stand; led to the back room's exit arrow
// they come out on the pavement between Salieri's door and the road (z -8.1).
// A person counts as out of Morello's this far from its room: past its west
// door, 2.8 m from the room, or its east one, in the room's east wall.
const CLEAR_OF_BAR = 4.5;
const BAR_FIRES: Readonly<Partial<Record<bars.Bar['id'], BarLayout>>> = {
    morello: {
        front: { x: -408.0, y: 17.45, z: 450.6 },
        people: [0, 3, 4],
        rooms: () => [
            { x: -411.0, y: 17.35, z: 456.6, radius: 1.4 },
            { x: -407.2, y: 17.35, z: 460.0, radius: 1.2 },
        ],
        street: [
            { x: -414.5, y: 17.6, z: 446.5 },
            { x: -413.0, y: 17.6, z: 446.0 },
            { x: -411.5, y: 17.6, z: 446.5 },
        ],
    },
    backroom: {
        interior: 'b:salieri-bar',
        front: { x: -1779.6, y: -5.5, z: 4.2 },
        people: [0, 1, 4],
        rooms: () => [
            { ...barRoom.toWorld({ x: 1.5, y: 0.02, z: -1.0 }), radius: 1.4 },
            { ...barRoom.toWorld({ x: -6.2, y: 0.02, z: 1.6 }), radius: 1.2 },
        ],
        street: [
            { x: -1777.8, y: -5.3, z: 1.0 },
            { x: -1776.3, y: -5.3, z: 0.6 },
            { x: -1774.8, y: -5.3, z: 1.0 },
        ],
    },
};

// How the bar is got into: by its arrow's interior, or walked into by its doors.
function entryOf(
    bar: bars.Bar,
    layout: BarLayout,
): { entry: BarEntry; door: Point; hall: Point } | null {
    if (layout.interior) {
        const interior = maps.current()?.interiors?.find((entry) => entry.id === layout.interior);

        return interior
            ? {
                  entry: { kind: 'arrow', interior },
                  door: interior.outside.position,
                  hall: interior.inside.position,
              }
            : null;
    }

    if (!bar.rooms) {
        return null;
    }

    return {
        entry: {
            kind: 'doors',
            inside: (position) => bars.contains(bar, position),
            clear: (position) => bars.gapFromRooms(bar, position) >= CLEAR_OF_BAR,
        },
        door: bar.door ?? layout.front,
        hall: bar.counter.position,
    };
}

// The bar that can burn on this map, if any: its fire layout and how it is got into.
function barVenue(): {
    bar: bars.Bar;
    layout: BarLayout;
    way: { entry: BarEntry; door: Point; hall: Point };
} | null {
    for (const bar of bars.bars()) {
        const layout = BAR_FIRES[bar.id];
        const way = layout ? entryOf(bar, layout) : null;

        if (!layout || !way || (bar.built && !barRoom.isBuilt())) {
            continue;
        }

        return { bar, layout, way };
    }

    return null;
}

export function startBarFire(): Incident | null {
    const venue = barVenue();

    if (!crews.size || !venue || !npcsAvailable()) {
        return null;
    }

    if ([...incidents.values()].some((incident) => incident.kind === 'bar')) {
        return null;
    }

    const { bar, layout, way } = venue;
    const front = Fire.create(layout.front, {
        radius: 2.0,
        height: 4,
        toughness: 3,
        growth: 0.012,
        damage: 18,
    });

    if (!front) {
        return null;
    }

    const world = bar.world;
    const inside = layout
        .rooms()
        .map(({ x, y, z, radius }) =>
            Fire.create(
                { x, y, z },
                {
                    radius,
                    height: 1.2,
                    toughness: 1.5,
                    growth: 0.01,
                    damage: 12,
                    ...(world === MAIN_WORLD ? {} : { virtualWorld: world }),
                },
            ),
        )
        .filter((fire): fire is Fire => fire !== null);
    const incident = newIncident(
        'bar',
        plain(way.door),
        `FIRE AT ${bar.name.toUpperCase()}`,
        front,
        {
            extra: inside,
            bar: { id: bar.id, name: bar.name, ...way, world, street: layout.street },
        },
    );

    incidents.set(incident.id, incident);
    // The staff and drinkers who were serving go; the ones caught in it stay.
    bars.setClosed(bar.id, true);
    const staff = [bar.bartender, ...bar.patrons];

    layout.people.forEach((slot, index) => {
        const spec = staff.at(slot);

        if (spec) {
            trap(incident, spec.position, CROUCHED, index, {
                model: spec.model,
                heading: spec.heading,
                world,
            });
        }
    });

    for (const player of World.getPlayers()) {
        if (bars.barOf(player)?.id === bar.id) {
            toast(player, 'Fire! The bar is burning. Get out through the door.', {
                title: bar.name,
                tone: 'bad',
                seconds: 6,
            });
        }
    }

    toastAll(
        `${bar.name} is on fire ${incident.where}, people inside. Firefighters: hose the front, go in through the door and lead them ` +
            'out with G. Medics: they will need you on the pavement.',
        { title: 'Fire', tone: 'bad', seconds: 10 },
    );

    feed('fire', [text('Fire', COLORS.red), text(` · ${bar.name} is burning with people inside`)]);
    presentIncidents();

    return incident;
}
