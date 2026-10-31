// The copper worm Hank needs (quest step 3): a hardware dealer's truck parks
// north of the village store while anyone still has to fetch one, its driver
// smoking beside it. Unbolting the worm off its bed calls the police; the
// dealer has another on the truck a minute later.
import * as profiles from '../lib/profiles';
import { Character, npcsAvailable } from '../lib/npcs';
import { feed, name, text, toast } from '../lib/notify';
import { PlayerSet } from '../lib/players';
import { headingFrom, MAIN_WORLD, onStreet, vehicleById } from '../lib/util';
import { occupied, spawnVehicle } from '../lib/vehicles';
import { hankSays, LINE, SMOKING } from './characters';
import { changed, opOf, QUEST, recordOf, RUM, setWanted, stageOf, TITLE, XP } from './operations';
import { DRIVER_SPOT, TRUCK_SPOT } from './places';

const DRIVER = { model: 'Delnik01.i3d', voice: 26 };
const TRUCK_MODEL = 'truckb00.i3d';
const IDLE_MS = 60_000;
const RESTOCK_MS = 60_000;
const COPPER_PAY = 100;

const truck = { vehicleId: null as number | null, coilTaken: false, restockAt: 0, idleSince: 0 };
const driver = new Character({
    onReady: (npc) => npc.playAnimation(SMOKING, { loop: true }),
});
// Players carrying a worm.
const carriers = new PlayerSet();

export function carrying(player: Player): boolean {
    return carriers.has(player.id);
}

/** The worm was dropped (a death) or the map went away. */
export function dropCoil(player: Player): boolean {
    return carriers.delete(player.id);
}

export function dropAllCoils(): void {
    carriers.clear();
}

export function coilTaken(): boolean {
    return truck.coilTaken;
}

// Somebody in town still has to unbolt the copper worm.
function wanted(): boolean {
    return World.getPlayers().some(
        (player) =>
            onStreet(player) && stageOf(player) === QUEST.COPPER && !carriers.has(player.id),
    );
}

export function truckVehicle(): Vehicle | null {
    return vehicleById(truck.vehicleId);
}

/** The truck's id while it stands, for its marker. */
export function truckId(): number | null {
    return truck.vehicleId;
}

export function removeTruck(): void {
    const vehicle = truckVehicle();

    if (vehicle && !occupied(vehicle)) {
        vehicle.destroy();
    }

    truck.vehicleId = null;
    driver.remove();
}

/** The mission change took the truck and its driver. */
export function forgetTruck(): void {
    truck.vehicleId = null;
    driver.forget();
}

export function tickTruck(now: number): void {
    if (!wanted()) {
        if (truck.vehicleId !== null || driver.npc) {
            truck.idleSince ||= now;

            if (now - truck.idleSince >= IDLE_MS) {
                removeTruck();
            }
        }

        return;
    }

    truck.idleSince = 0;

    if (truck.coilTaken && now >= truck.restockAt) {
        truck.coilTaken = false;
    }

    if (!truckVehicle() && typeof Vehicle !== 'undefined') {
        const vehicle = spawnVehicle(TRUCK_MODEL, TRUCK_SPOT, 0, {
            engineOff: true,
            state: { 'fr.distillery': 'truck' },
        });

        if (vehicle) {
            truck.vehicleId = vehicle.id;
        }
    }

    if (!driver.npc && npcsAvailable()) {
        driver.spawn(DRIVER.model, DRIVER_SPOT, headingFrom(DRIVER_SPOT, TRUCK_SPOT), {
            behaviour: 'neutral',
            voice: DRIVER.voice,
            virtualWorld: MAIN_WORLD,
        });
    }
}

/** The hold at the truck's bed is done. */
export function unbolt(player: Player): void {
    if (truck.coilTaken) {
        return;
    }

    truck.coilTaken = true;
    truck.restockAt = Date.now() + RESTOCK_MS;
    carriers.add(player.id);
    setWanted(player, 1);
    driver.say(LINE.robbed);
    toast(
        player,
        'You have the copper worm! The driver is shouting for the police. Get it to Hank at the still, on foot or in any car.',
        { title: TITLE, tone: 'gold', seconds: 8 },
    );
}

export function deliverCoil(player: Player): void {
    const op = opOf(player);
    const record = recordOf(player);

    if (!op || !record) {
        return;
    }

    carriers.delete(player.id);
    // Paid once per player, whichever operation they are in.
    const first = !record.copper;

    record.copper = true;
    op.unlocked = true;
    op.progress = QUEST.FIRST_FIRE;
    changed();

    if (!first) {
        hankSays(player, "Good copper. The still's yours to run now, you and yours.", LINE.nod);

        return;
    }

    op.molasses = Math.max(op.molasses, 1);
    profiles.addXp(player, XP.copper, 'Copper for Hank');
    player.giveMoney(COPPER_PAY);
    hankSays(
        player,
        "Ha! Good copper. The still's yours to run now, you and yours. I put a sack of molasses aside: press G at the still to fire it up.",
        LINE.nod,
        10,
    );

    feed('rum', [name(player, RUM), text(' started a rum operation somewhere in the woods')]);
}
