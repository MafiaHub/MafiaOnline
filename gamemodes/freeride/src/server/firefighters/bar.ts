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
import { incidents, newIncident, type Incident } from './model';
import { trap } from './rescue';

/** Where a fire in one of the bars (server/bars/) burns and who is caught in it. */
interface BarLayout {
    /** The bar's interior in lib/maps.ts. */
    interior: string;
    /** Flames out of the shop windows beside the door. */
    front: Point;
    /** Who is caught inside: [barman, ...drinkers] indices. */
    people: readonly number[];
    /** The fires in the room. */
    rooms: () => (Point & { radius: number })[];
    /** Where those led out stand on the pavement. */
    street: readonly Point[];
}

// Fires in a bar (server/bars/, its door in lib/maps.ts). Morello's bar on
// Lost Heaven Extended (models/morellobar.4ds): flames out of the shop
// windows east of the door arrow on the south front (z 451.5), and two in
// its room (floor y 17.3, x -415.7..-406.0, z 451.6..461.2): between the
// stools and the way in, and behind the counter's east end. Stock Lost
// Heaven's back-room bar (lib/bar_room.ts, room-local coordinates): flames
// out of Salieri's west shop window beside his door, one in the aisle in
// front of the counter and one at the far end. The people caught inside are
// the bar's own barman and drinkers, at their own places ([barman,
// ...drinkers] indices); led to the exit arrow they come out on the
// pavement between the door and the road (the road graph line at z 440.3
// for Morello's, z -8.1 for Salieri's).
const BAR_FIRES: Readonly<Partial<Record<bars.Bar['id'], BarLayout>>> = {
    morello: {
        interior: 'b:morello',
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

// The bar that can burn on this map, if any: its fire layout and its interior.
function barVenue(): { bar: bars.Bar; layout: BarLayout; interior: maps.MapInterior } | null {
    const map = maps.current();

    for (const bar of bars.bars()) {
        const layout = BAR_FIRES[bar.id];
        const interior = layout && map?.interiors?.find((entry) => entry.id === layout.interior);

        if (!layout || !interior || (bar.built && !barRoom.isBuilt())) {
            continue;
        }

        return { bar, layout, interior };
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

    const { bar, layout, interior } = venue;
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
        plain(interior.outside.position),
        `FIRE AT ${bar.name.toUpperCase()}`,
        front,
        {
            extra: inside,
            bar: { id: bar.id, name: bar.name, interior, world, street: layout.street },
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
