// The tobacco farm, in the countryside. A player signs on under the arrow at
// the barn (duty.ts, lib/stations.ts; two farm trucks wait beside it, which
// only farmhands drive), plants seedlings on the plots of the field beside
// the barn, waters them with a farm truck's hose at each stage, picks off
// hornworms, picks the ripe leaves (work.ts) and carries them to the barn to
// cure (curing.ts). The cured tobacco goes on a farm truck at the loading bay
// and is sold in the village, or exported at the container docks for far
// more (cargo.ts, market.ts); near the docks the whole city learns of the
// load, and whoever drives that truck in is paid. A loaded farm truck may be
// driven by anyone. Where the farm is on each map: sites.ts.
import * as menu from '../lib/menu';
import * as commands from '../lib/commands';
import * as profiles from '../lib/profiles';
import * as scenery from './scenery';
import { card, compass, toast } from '../lib/notify';
import { onStart, onStop } from '../lib/util';
import { events, every, onMissionChange } from '../lib/lifecycle';
import { forgetCargoMarkers, showCargoTo, tickBarn, tickCargo } from './cargo';
import { cure } from './curing';
import { clockOff, farmers, show } from './duty';
import { grow, ownPlants } from './field';
import { TITLE } from './owners';
import { ripe } from './plant';
import { COMPASS_PRIORITY, HUD } from './present';
import { buyers, here, site } from './sites';
import { cargo, load, persist, saveIfDue, stocks, unload } from './state';
import { tickFarmers } from './work';

const TICK_MS = 250;
const CARD_MS = 1000;
const MAX_STEP_MS = 2000;

let lastTick = Date.now();
let lastCardAt = 0;

/** Every load on a truck back into its owner's barn. */
function returnCargo(): void {
    for (const entry of cargo.values()) {
        unload(entry);
    }

    cargo.clear();
}

// The farm clock: plants grow and cure only while a map with the farm plays.
function tick(): void {
    const now = Date.now();
    const dt = Math.min(MAX_STEP_MS, Math.max(0, now - lastTick));

    lastTick = now;

    if (!here() || !World.isReady()) {
        return;
    }

    grow(dt, () => {
        for (const [player] of farmers.crew()) {
            toast(player, 'Rain! It waters the whole field for you.', { title: TITLE });
        }
    });

    cure(dt);
    scenery.sync(now);
    tickFarmers(dt);
    tickBarn(now);
    tickCargo();

    if (now - lastCardAt >= CARD_MS) {
        lastCardAt = now;

        for (const player of World.getPlayers()) {
            show(player);
        }
    }

    saveIfDue(now);
}

// Signing on happens under the barn's arrow; the command shows the way there
// and clocks off from anywhere.
commands.register(
    {
        name: 'farm',
        aliases: ['tobacco', 'farmer'],
        usage: '/farm',
        help: 'Show the way to the tobacco farm, or clock off.',
        category: 'City',
    },
    (player) => {
        if (farmers.has(player)) {
            clockOff(player);
        } else {
            farmers.start(player);
        }
    },
);

menu.provide('farm', (player) => {
    const key = profiles.keyOf(player);
    const plants = ownPlants(key);
    const stock = key ? stocks.get(key) : undefined;

    return {
        available: here(),
        onDuty: farmers.has(player),
        farmers: farmers.size,
        plants: plants.length,
        ripe: plants.filter(ripe).length,
        curing: stock?.curing.reduce((sum, batch) => sum + batch.n, 0) ?? 0,
        cured: stock?.cured.n ?? 0,
        export: buyers().some((buyer) => buyer.risky),
        earned: farmers.get(player)?.earned ?? 0,
        place: site()?.place ?? null,
    };
});

load();

onStart(() => {
    lastTick = Date.now();
    every(TICK_MS, tick);
});

// The shifts (duty.ts) have ended by now.
onStop(() => {
    returnCargo();
    persist();
    scenery.clear();
});

events.on('playerSpawn', showCargoTo);

onMissionChange(() => {
    for (const player of World.getPlayers()) {
        // Someone driving a load had its card and compass, not a shift.
        card(player, HUD, null);
        compass(player, HUD, COMPASS_PRIORITY, null);
    }

    returnCargo();
    // The mission's objects went with it.
    scenery.forget();
    forgetCargoMarkers();
    persist();
});

export const onDuty = (player: Player): boolean => farmers.has(player);
