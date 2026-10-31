// Farm work, a shift. A player signs on under the arrow at the barn (two
// farm trucks wait beside it, which only farmhands drive) and works the field
// (work.ts). A loaded farm truck may be driven by anyone: that is the risk of
// the run to a buyer.
import * as profiles from '../lib/profiles';
import * as stations from '../lib/stations';
import { feed, name, text, toast } from '../lib/notify';
import { formatMoney } from '../lib/util';
import { defineShift } from '../lib/shift';
import { MAX_PLANTS, SEEDLING_PRICE } from './field';
import { TITLE } from './owners';
import { COMPASS_PRIORITY, HUD, present } from './present';
import { LEAF, available, site, siteNow } from './sites';
import { cargo } from './state';

const TRUCK = 'trucka00.i3d';

// The hose runs from a farm truck's tank this far.
export const HOSE_TO_TRUCK = 30;
const TICK_MS = 250;
const CLOCK_OFF = 'Off duty. Your plants keep growing.';

/** Watering, picking off hornworms or picking the leaves: G held at a plant. */
export interface Action {
    kind: 'water' | 'pick' | 'harvest';
    plot: number | null;
    progress: number;
}

export interface Farmer {
    earned: number;
    jet: WaterJet | null;
    action: Action | null;
    /** The prompt last shown; undefined shows it again. */
    hint: string | null | undefined;
}

export const station = stations.define({
    id: 'farm',
    title: TITLE,
    crew: 'farmhands',
    mapLabel: 'Tobacco farm',
    color: LEAF,
    radar: 0xa7b85a,
    model: TRUCK,
    vehicleLabel: 'FARM TRUCK',
    pitch:
        "Grow tobacco: plant seedlings in the field beside the barn, water them with a farm truck's hose as they grow, pick the leaves " +
        'and cure them here, then drive the tobacco to a buyer. Press G here to sign on.',
    place: () => {
        const current = site();

        return current ? { signOn: current.signOn, spots: current.spots } : null;
    },
    available,
    onDuty: (player) => farmers.has(player),
    start: (player) => farmers.start(player),
    stop: clockOff,
    // A loaded truck can be taken by anyone: that is the risk of the run.
    mayDrive: (_player, vehicle) => cargo.has(vehicle.id),
});

export const farmers = defineShift<Farmer>({
    kind: 'farm',
    activity: 'Farm work',
    title: TITLE,
    hud: HUD,
    compassPriority: COMPASS_PRIORITY,
    flag: { key: 'fr.job', value: 'farm' },
    station: {
        station,
        missing:
            'The tobacco farm is in the countryside: play Lost Heaven Extended or The Countryside (/map).',
        directions: () =>
            `Sign on at ${siteNow().place}: follow the compass to the arrow and press G.`,
        // A loaded truck is anyone's to drive (station.mayDrive): it can go on to the buyer.
        keepsWheel: (vehicle) => cargo.has(vehicle.id),
    },
    // Another module took the player off the job: an arrest and the jail.
    interrupted: 'Your shift is over.',
    missionChange: 'Your shift ended with the map change. Your plants wait for you.',
    tickMs: TICK_MS,

    begin: (player) =>
        profiles.keyOf(player)
            ? { earned: 0, jet: null, action: null, hint: undefined }
            : 'Log in first.',

    started(player) {
        feed('farm', [name(player, LEAF), text(' went to work on the tobacco farm')]);
        toast(
            player,
            `You are on the farm. Walk into the field beside the barn and press G to plant a seedling (${formatMoney(SEEDLING_PRICE)}, ` +
                `up to ${MAX_PLANTS}). They need water at every stage: drive a farm truck up to the field and hold G by the dry ones. ` +
                'Hold G at a ripe plant to pick it, carry the leaves to this arrow to cure, then load a farm truck at the loading bay and sell. ' +
                'Back under the arrow, G clocks you off.',
            { title: TITLE, tone: 'gold', seconds: 14 },
        );

        show(player);
    },

    end(player, farmer, how) {
        farmer.jet?.destroy();

        if (how.online) {
            show(player);
        }
    },

    summary: (farmer) => `You earned ${formatMoney(farmer.earned)} this shift.`,
});

export function clockOff(player: Player): void {
    farmers.stop(player, CLOCK_OFF);
}

/** The farm truck whose hose reaches the player, if any. */
export function truckNear(player: Player): { vehicle: Vehicle; gap: number } | null {
    const truck = station.nearestVehicle(player.position);

    return truck && truck.gap <= HOSE_TO_TRUCK ? truck : null;
}

/** The player's card, compass and markers; only changes are sent. */
export function show(player: Player): void {
    const farmer = farmers.get(player);

    present(
        player,
        farmer ? { earned: farmer.earned, truckNear: truckNear(player) !== null } : null,
    );
}
