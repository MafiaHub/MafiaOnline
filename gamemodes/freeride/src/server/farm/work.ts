// Work on the field, all on G: plant a seedling on a free plot, water the dry
// plants with a farm truck's hose, pick off hornworms, pick the ripe leaves.
// Crewmates' plants count as one's own; anyone may water or weed a
// neighbour's plant (watering earns a tip), but the leaves are the owner's.
import * as jobkey from '../lib/jobkey';
import * as life from '../lib/life';
import * as profiles from '../lib/profiles';
import { toast } from '../lib/notify';
import { flatDistance, formatMoney, playable } from '../lib/util';
import { HOSE_TO_TRUCK, farmers, truckNear, type Action, type Farmer } from './duty';
import {
    MAX_PLANTS,
    SEEDLING_PRICE,
    freePlot,
    grade,
    inField,
    ownPlants,
    plantsNear,
    plotPosition,
    remove,
    sow,
    water,
} from './field';
import { TITLE, mine } from './owners';
import { ripe } from './plant';
import { here, type Plot } from './sites';
import { changed, field, loading, stockOf, type Plant } from './state';

const NEIGHBOUR_TIP = 5;
const WATER_MS = 2500;
const PICK_MS = 2000;
const HARVEST_MS = 2000;
const HOSE_REACH = 9;
const HOSE_PRESSURE = 11;
const TOUCH = 2.2;
const XP = { plant: 2, water: 3, pick: 4, harvest: 6 };

type Choice = { kind: 'plant'; plot: Plot } | { kind: Action['kind']; plant: Plant };

function stopAction(player: Player, farmer: Farmer): void {
    farmer.action = null;
    farmer.jet?.stop();
    farmer.hint = undefined;
    jobkey.prompt(player, null);
}

function dryInReach(player: Player): Plant | null {
    return plantsNear(player.position, HOSE_REACH).find((entry) => entry.plant.dry)?.plant ?? null;
}

// What G does where the player stands, strongest first.
function choose(player: Player): Choice | null {
    const close = plantsNear(player.position, TOUCH);
    const ripePlant = close.find((entry) => ripe(entry.plant))?.plant;

    if (ripePlant) {
        return { kind: 'harvest', plant: ripePlant };
    }

    const pest = close.find((entry) => entry.plant.pest)?.plant;

    if (pest) {
        return { kind: 'pick', plant: pest };
    }

    // Standing on an empty plot plants it; standing by a planted one waters.
    const plot = inField(player.position) ? freePlot(player.position) : null;

    if (plot && ownPlants(profiles.keyOf(player)).length < MAX_PLANTS) {
        return { kind: 'plant', plot };
    }

    const dry = dryInReach(player);

    return dry ? { kind: 'water', plant: dry } : null;
}

function hintFor(player: Player): string | null {
    if (player.getVehicle()) {
        return null;
    }

    const choice = choose(player);

    switch (choice?.kind) {
        case undefined:
            return null;
        case 'harvest':
            return mine(player, choice.plant)
                ? '[G] HOLD TO PICK THE LEAVES'
                : `${choice.plant.ownerName.toUpperCase()}'S TOBACCO`;
        case 'pick':
            return '[G] HOLD TO PICK OFF THE HORNWORMS';
        case 'water':
            return truckNear(player)
                ? '[G] HOLD TO WATER'
                : `DRY · BRING A FARM TRUCK WITHIN ${HOSE_TO_TRUCK} M`;
        case 'plant':
            return `[G] PLANT A SEEDLING · ROW ${choice.plot.row} · ${formatMoney(SEEDLING_PRICE)}`;
    }
}

function plantSeedling(player: Player, plot: Plot): void {
    const key = profiles.keyOf(player);

    if (!key) {
        return;
    }

    if (!player.trySpendMoney(SEEDLING_PRICE)) {
        toast(player, `A seedling costs ${formatMoney(SEEDLING_PRICE)}.`, {
            title: TITLE,
            tone: 'bad',
            seconds: 3,
        });

        return;
    }

    sow(player, key, plot);
    profiles.addXp(player, XP.plant, 'Planted tobacco');
    const count = ownPlants(key).length;

    if (count === MAX_PLANTS) {
        toast(player, `That is ${MAX_PLANTS} plants, as many as one farmhand can tend.`, {
            title: TITLE,
            seconds: 5,
        });
    } else if (count === 1) {
        toast(
            player,
            `Planted. A seedling grows only once watered: bring a farm truck within ${HOSE_TO_TRUCK} m and hold G by it.`,
            { title: TITLE, seconds: 7 },
        );
    }
}

function finishAction(player: Player, farmer: Farmer, action: Action, plant: Plant): void {
    if (action.kind === 'water') {
        water(plant);
        profiles.addXp(player, XP.water, 'Watered tobacco');

        // Crewmates' plants count as one's own: no tip between them.
        if (!mine(player, plant)) {
            player.giveMoney(NEIGHBOUR_TIP);
            farmer.earned += NEIGHBOUR_TIP;
        }

        return;
    }

    if (action.kind === 'pick') {
        plant.pest = false;
        changed();
        profiles.addXp(player, XP.pick, 'Picked off hornworms');

        return;
    }

    const key = profiles.keyOf(player);

    if (!key) {
        return;
    }

    const stock = stockOf(key, player.nickname);
    const quality = grade(plant);

    stock.leaves.n++;
    stock.leaves.q += quality;
    remove(plant);
    profiles.addXp(player, XP.harvest, 'Picked tobacco');

    if (stock.leaves.n === 1) {
        toast(
            player,
            `Leaves picked (quality ${quality}%). Carry them to the barn's arrow: they cure there.`,
            { title: TITLE, seconds: 6 },
        );
    }
}

// The hose follows the dry plants in reach, one after another, while G is held.
function progressWatering(player: Player, farmer: Farmer, action: Action, dt: number): void {
    if (!truckNear(player)) {
        stopAction(player, farmer);
        toast(
            player,
            `The hose runs from a farm truck's tank and reaches ${HOSE_TO_TRUCK} m from it.`,
            { title: TITLE, tone: 'bad', seconds: 4 },
        );

        return;
    }

    let plant = action.plot === null ? undefined : field.get(action.plot);

    if (
        !plant ||
        !plant.dry ||
        flatDistance(plotPosition(plant.plot), player.position) > HOSE_REACH
    ) {
        plant = dryInReach(player) ?? undefined;
        action.plot = plant?.plot ?? null;
        action.progress = 0;
    }

    if (!plant) {
        farmer.jet?.stop();
        jobkey.prompt(player, 'NOTHING DRY IN REACH · RELEASE G');

        return;
    }

    const spot = plotPosition(plant.plot);

    farmer.jet?.aimAt({ x: spot.x, y: spot.y + 0.3, z: spot.z });
    action.progress += dt;
    jobkey.prompt(player, 'WATERING…', Math.min(1, action.progress / WATER_MS));

    if (action.progress >= WATER_MS) {
        finishAction(player, farmer, action, plant);
        action.plot = null;
        action.progress = 0;
    }
}

function progressAction(player: Player, farmer: Farmer, action: Action, dt: number): void {
    if (!jobkey.isHeld(player) || player.getVehicle()) {
        stopAction(player, farmer);

        return;
    }

    if (action.kind === 'water') {
        progressWatering(player, farmer, action, dt);

        return;
    }

    const plant = action.plot === null ? undefined : field.get(action.plot);
    const valid =
        plant !== undefined &&
        flatDistance(plotPosition(plant.plot), player.position) <= TOUCH + 0.8 &&
        (action.kind === 'harvest' ? ripe(plant) : plant.pest);

    if (!valid) {
        stopAction(player, farmer);

        return;
    }

    action.progress += dt;
    const needed = action.kind === 'harvest' ? HARVEST_MS : PICK_MS;

    jobkey.prompt(
        player,
        action.kind === 'harvest' ? 'PICKING THE LEAVES…' : 'PICKING OFF THE HORNWORMS…',
        Math.min(1, action.progress / needed),
    );

    if (action.progress >= needed) {
        finishAction(player, farmer, action, plant);
        stopAction(player, farmer);
    }
}

/** A step of every farmhand's work, `dt` after the last. */
export function tickFarmers(dt: number): void {
    for (const [player, farmer] of farmers.crew()) {
        // Taken off the job (an arrest): the shift ends it on its next tick.
        if (life.activity(player)?.kind !== 'farm') {
            continue;
        }

        if (!playable(player)) {
            if (farmer.action) {
                stopAction(player, farmer);
            }

            continue;
        }

        if (farmer.action) {
            progressAction(player, farmer, farmer.action, dt);
            continue;
        }

        if (loading.has(player.id)) {
            continue;
        }

        const hint = hintFor(player);

        if (hint !== farmer.hint) {
            farmer.hint = hint;
            jobkey.prompt(player, hint);
        }
    }
}

jobkey.register({
    press(player) {
        const farmer = farmers.get(player);

        if (!farmer || !playable(player) || player.getVehicle() || !here()) {
            return false;
        }

        const choice = choose(player);

        if (!choice) {
            return false;
        }

        if (choice.kind === 'plant') {
            plantSeedling(player, choice.plot);
            farmer.hint = undefined;

            return true;
        }

        if (choice.kind === 'harvest' && !mine(player, choice.plant)) {
            toast(
                player,
                `This is ${choice.plant.ownerName}'s plant. Water it or pick off its hornworms if you like; the leaves are theirs.`,
                { title: TITLE, tone: 'bad', seconds: 4 },
            );

            return true;
        }

        if (choice.kind === 'water') {
            if (!truckNear(player)) {
                toast(
                    player,
                    `The hose runs from a farm truck: park one within ${HOSE_TO_TRUCK} m of the plants.`,
                    { title: TITLE, tone: 'bad', seconds: 4 },
                );

                return true;
            }

            farmer.jet ??= WaterJet.create(player, { pressure: HOSE_PRESSURE });

            if (!farmer.jet) {
                toast(player, 'No hose free right now. Try again shortly.', {
                    tone: 'bad',
                    seconds: 3,
                });

                return true;
            }
        }

        farmer.action = { kind: choice.kind, plot: choice.plant.plot, progress: 0 };

        return true;
    },
    release(player) {
        const farmer = farmers.get(player);

        if (farmer?.action) {
            stopAction(player, farmer);
        }
    },
});
