// Four places with a reason to visit: Pete sells weapons, the pump's
// attendants sell fuel, the pharmacy patches wounds, and Salieri's back window
// starts a courier run.
// Positions were read from the stock a1.dta mission scene2.bin frames:
//   MISE19-MESTO: "1dvere u peteho" (Pete's doorway)
//   FREERIDE and FREERIDENOC: "_pumpa2", "lekarna", "Salieri_save".
// Both selected Free Ride scenes contain the same latter three positions.
import * as profiles from './lib/profiles';
import * as life from './lib/life';
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { Cooldown } from './lib/cooldowns';
import { events, every, onStart } from './lib/lifecycle';
import { toast, card, compass, type Tone } from './lib/notify';
import { fields } from './lib/payload';
import { PlayerMap } from './lib/players';
import { distance, emit, onClient, formatMoney, formatMinutes, type Point } from './lib/util';

const WEAPON = Object.freeze({ BAT: 4, COLT: 6, THOMPSON: 10, SHOTGUN: 11, GRENADE: 15 });
const START_FUEL_FRACTION = 0.12;
// The retail attendants (Pumper) fuel the car at this price per litre.
const FUEL_PRICE_PER_LITRE = 3;
const SHOP_REACH = 3.5;
const PURCHASE_COOLDOWN_MS = 450;
const COURIER_TIME_MS = 8 * 60_000;
const COURIER_REWARD = 180;
const COURIER_XP = 60;
const COURIER_TICK_MS = 5000;
const COURIER_COMPASS_PRIORITY = 60;
const LOW_FUEL_RETRY_MS = 500;
const LOW_FUEL_RETRY_COUNT = 30;

/** Something a shop sells. */
export interface Offer {
    id: string;
    name: string;
    /** null: priced at the pump; 0: free (accepted, not bought). */
    price: number | null;
    note: string;
}

/** A shop, as client/shops.ts draws it. */
export interface Shop {
    id: string;
    name: string;
    tagline: string;
    position: Point;
    /** Which way the clerk faces, when the shop says. */
    facing?: Point;
    /** The door hinge's frame, and where it is when the frame is not found. */
    entranceFrame?: string;
    entrance?: Point;
    model: string | null;
    animation: string | null;
    offers: Offer[];
}

export const SHOPS: readonly Shop[] = [
    {
        id: 'pete',
        name: "Yellow Pete's",
        tagline: 'No questions after dark',
        position: { x: 61.13562, y: 6.12056, z: 113.52177 },
        // Box24 is the shop door hinge in both Free Ride scenes. Pete faces
        // away from the hinge toward players approaching the doorway.
        entranceFrame: 'Box24',
        entrance: { x: 60.58458, y: 6.09017, z: 113.82933 },
        model: 'ProdZ1.i3d',
        animation: 'BorecStat.i3d',
        offers: [
            { id: 'colt', name: 'Colt .45', price: 90, note: 'Six in the chamber, eighteen spare' },
            {
                id: 'shells',
                name: 'Shotgun & shells',
                price: 210,
                note: 'For when a quiet word will not do',
            },
            {
                id: 'thompson',
                name: 'Thompson & drums',
                price: 320,
                note: 'A loud answer to a bad question',
            },
            {
                id: 'grenades',
                name: 'Three grenades',
                price: 95,
                note: 'Keep them out of the glovebox',
            },
        ],
    },
    // The stock attendants serve the cars here; no scripted one stands in.
    {
        id: 'pump',
        name: 'The Night Pump',
        tagline: 'A full tank buys a little more time',
        position: { x: -110.6361, y: 8.4794, z: -135.5433 },
        model: null,
        animation: null,
        offers: [
            {
                id: 'petrol',
                name: 'A full tank',
                price: null,
                note: `$${FUEL_PRICE_PER_LITRE} per litre · stop at a pump, engine off, and ask the attendant`,
            },
        ],
    },
    // The "lekarna" frame floats 1.5 m over the hospital's front landing,
    // whose floor is at y 13.2 (the door 3DVERE4 at z 763.5): the clerk
    // stands on the landing below it, facing the street (-z, away from the
    // doors).
    {
        id: 'pharmacy',
        name: 'The Dispensary',
        tagline: 'The city keeps its own hours',
        position: { x: -761.3176, y: 13.25, z: 762.4796 },
        facing: { x: 0, y: 0, z: -1 },
        model: 'recep.i3d',
        animation: null,
        offers: [
            {
                id: 'bandage',
                name: 'Bandages & tincture',
                price: 65,
                note: 'Restore up to 45 health',
            },
        ],
    },
    {
        id: 'salieri',
        name: "Salieri's Back Window",
        tagline: 'Coffee, pie, and sealed envelopes',
        position: { x: -1774.5226, y: -4.5073, z: 5.2466 },
        model: 'Barman01.i3d',
        animation: null,
        offers: [
            { id: 'coffee', name: 'Coffee & pie', price: 18, note: 'Restore up to 18 health' },
            {
                id: 'courier',
                name: 'A sealed envelope',
                price: 0,
                note: `Take it to Pete in eight minutes · earn $${COURIER_REWARD}`,
            },
        ],
    },
];

const PETE = SHOPS[0];

// How much each health offer restores.
const HEALING: Readonly<Record<string, number>> = { bandage: 45, coffee: 18 };

const PUMPER_REFUSALS: Readonly<Record<Exclude<PumperRefusedInfo['reason'], 'money'>, string>> = {
    disabled: 'The pumps are closed.',
    unavailable: 'Nobody is minding the pumps right now.',
    busy: 'The attendant is busy with another car. Wait your turn.',
    full: 'Your tank is full already.',
};

interface CourierRun {
    startedAt: number;
}

interface LowFuel {
    generation: number;
    retries: number;
}

const purchases = new Cooldown(PURCHASE_COOLDOWN_MS);
const courier = new PlayerMap<CourierRun>();
// By vehicle id: personal cars still to be drained to their starting fuel.
const lowFuelPending = new Map<number, LowFuel>();

// The city maps have the shops; the countryside has none.
function shopsOpen(): boolean {
    return Boolean(maps.current()?.shops);
}

// On Lost Heaven Extended Salieri's front door opens into his bar (bars/),
// where the clerk would stand in the doorway; the window keeps serving
// without him.
function shownShops(): readonly Shop[] {
    return maps.current()?.id === 'freeride_extended'
        ? SHOPS.map((shop) => (shop.id === 'salieri' ? { ...shop, model: null } : shop))
        : SHOPS;
}

function shopState(player: Player): void {
    if (!shopsOpen()) {
        return;
    }

    emit(player, 'fr:shops:state', {
        generation: World.getMissionGeneration(),
        shops: shownShops(),
        courier: courier.has(player.id),
    });
}

function chargeAndApply(player: Player, cost: number, apply: () => boolean): boolean {
    if (!player.trySpendMoney(cost)) {
        toast(player, `You need ${formatMoney(cost)}; you have ${formatMoney(player.money)}.`, {
            tone: 'bad',
        });

        return false;
    }

    if (apply()) {
        return true;
    }

    player.giveMoney(cost);
    toast(player, 'The deal fell through; your money was returned.', { tone: 'bad' });

    return false;
}

function buyWeapon(player: Player, offer: Offer): boolean {
    switch (offer.id) {
        case 'colt':
            return player.hasWeapon(WEAPON.COLT)
                ? player.giveAmmo(WEAPON.COLT, 18)
                : player.giveWeapon(WEAPON.COLT, 6, 18, true);
        case 'shells':
            return player.hasWeapon(WEAPON.SHOTGUN)
                ? player.giveAmmo(WEAPON.SHOTGUN, 24)
                : player.giveWeapon(WEAPON.SHOTGUN, 8, 24, true);
        case 'thompson':
            return player.hasWeapon(WEAPON.THOMPSON)
                ? player.giveAmmo(WEAPON.THOMPSON, 100)
                : player.giveWeapon(WEAPON.THOMPSON, 50, 100, true);
        // A throwable's count is its loaded value; addItem adds to it.
        case 'grenades':
            return player.hasWeapon(WEAPON.GRENADE)
                ? player.addItem(WEAPON.GRENADE, 3)
                : player.giveWeapon(WEAPON.GRENADE, 3, 0, true);
        default:
            return false;
    }
}

// The attendant fuels the car himself (Pumper, server-charged); the counter
// only tells how.
function buyFuel(player: Player): void {
    toast(
        player,
        'Drive up to a pump, switch the engine off and press the exit key: the attendant comes to you.',
    );
}

function buyHealth(player: Player, offer: Offer, price: number): void {
    const health = Math.min(100, player.health + (HEALING[offer.id] ?? 0));

    if (health <= player.health) {
        toast(player, 'You are in good shape already.');

        return;
    }

    if (chargeAndApply(player, price, () => player.setHealth(health))) {
        toast(player, `${offer.name}: health ${Math.round(player.health)}/100.`, { tone: 'good' });
    }
}

function courierCard(player: Player): void {
    const job = courier.get(player.id);

    card(
        player,
        'courier',
        job
            ? {
                  title: 'Courier run',
                  subtitle: 'Deliver the envelope to Yellow Pete',
                  rows: [
                      ['Pay', formatMoney(COURIER_REWARD)],
                      ['Time left', formatMinutes(job.startedAt + COURIER_TIME_MS - Date.now())],
                  ],
                  progress: Math.max(0, 1 - (Date.now() - job.startedAt) / COURIER_TIME_MS),
                  tone: 'gold',
              }
            : null,
    );
}

function endCourier(player: Player, message: string | null, tone: Tone = 'bad'): void {
    if (!courier.delete(player.id)) {
        return;
    }

    life.end(player, 'courier');
    courierCard(player);
    compass(player, 'courier', COURIER_COMPASS_PRIORITY, null);
    shopState(player);

    if (message) {
        toast(player, message, { tone });
    }
}

function startCourier(player: Player): void {
    if (courier.has(player.id)) {
        toast(player, 'You already have an envelope to deliver.');

        return;
    }

    if (!life.begin(player, 'courier', 'Courier run')) {
        toast(player, life.busyMessage(player) ?? '', { tone: 'bad' });

        return;
    }

    courier.set(player.id, { startedAt: Date.now() });
    toast(
        player,
        `Take the sealed envelope to Yellow Pete. Eight minutes, ${formatMoney(COURIER_REWARD)}.`,
        { title: 'Courier run', tone: 'gold' },
    );

    compass(player, 'courier', COURIER_COMPASS_PRIORITY, { position: PETE.position });
    courierCard(player);
    shopState(player);
}

function buy(player: Player, request: unknown): void {
    const { generation, shopId, offerId } = fields(request);

    if (
        !shopsOpen() ||
        !World.isReady() ||
        !player.spawned ||
        !player.alive ||
        player.missionGeneration !== World.getMissionGeneration() ||
        generation !== World.getMissionGeneration()
    ) {
        return;
    }

    const shop = SHOPS.find((entry) => entry.id === shopId);
    const offer = shop?.offers.find((entry) => entry.id === offerId);

    if (!shop || !offer || distance(player.position, shop.position) > SHOP_REACH) {
        return;
    }

    if (purchases.take(player) > 0) {
        return;
    }

    if (offer.id === 'petrol') {
        buyFuel(player);

        return;
    }

    if (offer.id === 'courier') {
        startCourier(player);

        return;
    }

    // Only the pump's petrol is priced at the pump.
    if (offer.price === null) {
        return;
    }

    if (offer.id === 'bandage' || offer.id === 'coffee') {
        buyHealth(player, offer, offer.price);

        return;
    }

    if (chargeAndApply(player, offer.price, () => buyWeapon(player, offer))) {
        toast(
            player,
            `${offer.name} for ${formatMoney(offer.price)}. Pete counts the bills twice.`,
            {
                tone: 'good',
            },
        );
    }
}

function deliver(player: Player): void {
    const job = courier.get(player.id);

    if (
        !job ||
        !shopsOpen() ||
        !player.spawned ||
        !player.alive ||
        distance(player.position, PETE.position) > SHOP_REACH
    ) {
        return;
    }

    if (Date.now() - job.startedAt > COURIER_TIME_MS) {
        endCourier(player, "The envelope arrived too late. Nobody at Pete's will take it.");

        return;
    }

    player.giveMoney(COURIER_REWARD);
    profiles.bump(player, 'deliveries');
    profiles.addXp(player, COURIER_XP, 'Courier run');
    endCourier(
        player,
        `Pete takes the envelope without a word. ${formatMoney(COURIER_REWARD)} earned.`,
        'good',
    );
}

every(COURIER_TICK_MS, () => {
    for (const player of World.getPlayers()) {
        const job = courier.get(player.id);

        if (!job) {
            continue;
        }

        if (Date.now() - job.startedAt > COURIER_TIME_MS) {
            endCourier(player, 'The courier run expired. Salieri will not be pleased.');
        } else {
            courierCard(player);
        }
    }
});

// A car's fuel can be set once its dynamics have reported: tried for a while.
every(LOW_FUEL_RETRY_MS, () => {
    for (const [id, entry] of lowFuelPending) {
        const vehicle = World.getVehicles().find((candidate) => candidate.id === id);

        if (
            !vehicle ||
            vehicle.state.get('fr.fullTank') ||
            entry.generation !== World.getMissionGeneration() ||
            ++entry.retries > LOW_FUEL_RETRY_COUNT
        ) {
            lowFuelPending.delete(id);
            continue;
        }

        if (!vehicle.dynamicsValid || vehicle.fuelTankCapacity <= 0) {
            continue;
        }

        const startingFuel = Math.min(vehicle.fuel, vehicle.fuelTankCapacity * START_FUEL_FRACTION);

        if (vehicle.setFuel(startingFuel)) {
            lowFuelPending.delete(id);
        }
    }
});

onStart(() => Pumper.setPrice(FUEL_PRICE_PER_LITRE));

events.on('missionChange', () => {
    lowFuelPending.clear();

    for (const player of World.getPlayers()) {
        endCourier(player, null);
    }
});

// Personal cars start nearly dry so the pump has a use; race and event cars
// mark themselves full with the "fr.fullTank" state key.
events.on('vehicleSpawn', (vehicle) => {
    if (shopsOpen() && !vehicle.traffic) {
        lowFuelPending.set(vehicle.id, { generation: World.getMissionGeneration(), retries: 0 });
    }
});

events.on('vehicleDestroy', (vehicle) => lowFuelPending.delete(vehicle.id));

events.on('vehicleRefuel', (_vehicle, player, info) => {
    if (!player) {
        return;
    }

    if (info.litres > 0) {
        toast(
            player,
            `The attendant put in ${info.litres.toFixed(1)} litres for ${formatMoney(info.cost)}.`,
            { tone: 'good' },
        );
    } else {
        toast(player, 'The attendant pumped, but you had nothing to pay with.', { tone: 'bad' });
    }
});

events.on('pumperRefused', (player, _vehicle, info) => {
    toast(
        player,
        info.reason === 'money'
            ? `A full tank costs ${formatMoney(info.cost)}; you have ${formatMoney(player.money)}.`
            : (PUMPER_REFUSALS[info.reason] ?? 'The attendant waves you off.'),
        { tone: 'bad' },
    );
});

events.on('playerSpawn', shopState);
events.on('playerRespawn', shopState);
events.on('playerDeath', (player) =>
    endCourier(player, 'You dropped the envelope when you went down.'),
);

onClient('fr:shop:buy', (player, request: unknown) => buy(player, request));
onClient('fr:shop:deliver', (player) => deliver(player));

commands.register(
    {
        name: 'courier',
        usage: '/courier [cancel]',
        help: 'Check or abandon your courier run.',
        category: 'City',
    },
    (player, args) => {
        const job = courier.get(player.id);

        if (args[0]?.toLowerCase() === 'cancel') {
            if (job) {
                endCourier(player, 'You burned the envelope. No pay today.');
            }

            return;
        }

        toast(
            player,
            job
                ? `Deliver to Yellow Pete: ${formatMinutes(job.startedAt + COURIER_TIME_MS - Date.now())} left.`
                : "Salieri's back window has courier work. Press N there to pick the envelope, B to take it.",
        );
    },
);

menu.provide('places', () =>
    SHOPS.map((shop) => ({
        id: shop.id,
        name: shop.name,
        tagline: shop.tagline,
        offers: shop.offers.map((offer) => offer.name),
    })),
);
