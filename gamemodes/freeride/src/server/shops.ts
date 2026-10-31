// Four places with a reason to visit: Pete sells and buys back weapons in his
// cellar, the pump's attendants sell fuel, the pharmacy patches wounds, and
// Salieri's back window starts a courier run.
// Positions were read from the stock a1.dta mission data:
//   MISE19-MESTO: Pete's spot in his cellar, web point 9 (server/gunshop.ts)
//   FREERIDE and FREERIDENOC: "_pumpa2", "lekarna", "Salieri_save".
// Both selected Free Ride scenes contain the same latter three positions.
import * as profiles from './lib/profiles';
import * as life from './lib/life';
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import * as hospitalRoom from './lib/hospital_room';
import * as pete from './pete';
import { Cooldown } from './lib/cooldowns';
import { events, every, onStart } from './lib/lifecycle';
import { toast, card, compass, type Tone } from './lib/notify';
import { fields } from './lib/payload';
import { PlayerMap, SentCache } from './lib/players';
import {
    distance,
    emit,
    onClient,
    onStreet,
    formatMoney,
    formatMinutes,
    MAIN_WORLD,
    pick,
    type Point,
} from './lib/util';
import {
    buyback,
    counterLines,
    dealFor,
    GUNS,
    receiptsFor,
    refusal,
    SIGN,
    SPOT,
    withReceipt,
    type Gun,
    type Receipts,
} from './gunshop';

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
// Pete's card follows what a customer carries while they are this near him.
const PETE_CARD_RANGE = 12;
const PETE_CARD_MS = 1000;

/** Something a shop sells. */
export interface Offer {
    id: string;
    name: string;
    /** null: priced at the pump; 0: free (accepted, not bought). */
    price: number | null;
    note: string;
    /** The shop pays the price for it (0: it will not take it). */
    sell?: boolean;
}

/** A shop, as client/shops.ts draws it. */
export interface Shop {
    id: string;
    name: string;
    tagline: string;
    position: Point;
    /** Which way the clerk faces, when the shop says. */
    facing?: Point;
    /** Where its name hangs, when not over the counter (a cellar's street door). */
    sign?: Point;
    model: string | null;
    animation: string | null;
    offers: Offer[];
}

export const SHOPS: readonly Shop[] = [
    // Pete is server/pete.ts's NPC down in his cellar; his card is each
    // customer's own (peteLines): rounds for what they carry, and what he
    // buys back.
    {
        id: 'pete',
        name: "Yellow Pete's",
        tagline: 'Down the stairs. No questions after dark',
        position: SPOT.position,
        sign: SIGN,
        model: null,
        animation: null,
        offers: GUNS.map(({ id, name, price, note }) => ({ id, name, price, note })),
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

// What Pete says over a deal.
const SOLD_LINES: readonly string[] = [
    'Clean, oiled, never been to court.',
    'Good choice. You did not get it here.',
    'Treat it gentle and it treats you the same.',
];
const ROUNDS_LINES: readonly string[] = [
    'Fresh from the crate.',
    'That should keep it talking a while.',
];
const BOUGHT_BACK_LINES: readonly string[] = [
    'I will take it back. Not for what you paid, of course.',
    'A little worn, but it will sell again.',
    'Wiped clean? Good. It never left my wall.',
];

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
// The Pete lines each player was last sent.
const peteCards = new SentCache();
const courier = new PlayerMap<CourierRun>();
// By vehicle id: personal cars still to be drained to their starting fuel.
const lowFuelPending = new Map<number, LowFuel>();

// The city maps have the shops; the countryside has none.
function shopsOpen(): boolean {
    return Boolean(maps.current()?.shops);
}

// Where a map's hospital has wards (lib/hospital_room.ts) the Dispensary
// sells at their reception desk instead of on the front landing: its counter
// is the receptionist's spot, she is the hospital's own NPC
// (hospital/staff.ts), so the client stands no clerk there.
function inWards(shop: Shop): Shop {
    return {
        ...shop,
        tagline: 'Ask at the reception desk',
        position: hospitalRoom.placed(hospitalRoom.SPOTS.receptionist).position,
        model: null,
        animation: null,
    };
}

/**
 * The shops of the loaded map, as players find them. On Lost Heaven
 * Extended Salieri's front door opens into his bar (bars/), where the clerk
 * would stand in the doorway; the window keeps serving without him.
 */
export function shopsHere(): readonly Shop[] {
    const map = maps.current();

    return SHOPS.map((shop) => {
        if (shop.id === 'salieri' && map?.id === 'freeride_extended') {
            return { ...shop, model: null };
        }

        return shop.id === 'pharmacy' && map?.hospital?.wards ? inWards(shop) : shop;
    });
}

/** The virtual world a shop serves in: the wards' for the Dispensary inside them, the street's otherwise. */
export function shopWorld(shop: Shop): number {
    return shop.id === 'pharmacy' && maps.current()?.hospital?.wards
        ? hospitalRoom.WORLD
        : MAIN_WORLD;
}

// Guns Pete sold the player and has not bought back (lib/profiles.ts keeps
// them with the player's record, as it keeps the guns they carry).
function receipts(player: Player): Receipts | undefined {
    return profiles.get(player)?.gunReceipts;
}

function receipt(player: Player, gun: Gun, delta: number): void {
    profiles.setField(player, 'gunReceipts', withReceipt(receipts(player), gun.weaponId, delta));
}

/** Pete's card for this player: what is on his wall, and what he would buy back from them. */
function peteLines(player: Player): Offer[] {
    const inventory = player.getInventory();

    return counterLines(
        (inventory?.weapons ?? []).map((weapon) => weapon.weaponId),
        inventory?.selected ?? 0,
        receipts(player),
    );
}

function shopState(player: Player): void {
    if (!shopsOpen()) {
        return;
    }

    const lines = peteLines(player);

    peteCards.set(player.id, JSON.stringify(lines));
    emit(player, 'fr:shops:state', {
        generation: World.getMissionGeneration(),
        shops: shopsHere().map((shop) => (shop.id === PETE.id ? { ...shop, offers: lines } : shop)),
        courier: courier.has(player.id),
    });
}

function chargeAndApply(
    player: Player,
    cost: number,
    apply: () => boolean,
    failed = 'The deal fell through; your money was returned.',
): boolean {
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
    toast(player, failed, { tone: 'bad' });

    return false;
}

// A piece off the wall. A throwable's count is its loaded value, and addItem
// adds to the ones carried; a knife or a gun is one of a kind.
function buyGun(player: Player, gun: Gun): void {
    const carried = player.hasWeapon(gun.weaponId);

    if (carried && gun.kind !== 'throwable') {
        toast(player, `You carry a ${gun.name} already.`);

        return;
    }

    const handed = chargeAndApply(
        player,
        gun.price,
        () =>
            carried
                ? player.addItem(gun.weaponId, gun.loaded)
                : player.giveWeapon(gun.weaponId, gun.loaded, gun.reserve, true),
        'You have no room to carry it. Your money was returned.',
    );

    if (!handed) {
        return;
    }

    if (gun.kind === 'firearm') {
        receipt(player, gun, 1);
    }

    toast(player, `${gun.name} for ${formatMoney(gun.price)}.`, { tone: 'good' });
    pete.says(player, pick(SOLD_LINES), { voice: true, deal: true });
}

function buyRounds(player: Player, gun: Gun): void {
    if (!gun.ammo || !player.hasWeapon(gun.weaponId)) {
        return;
    }

    const { rounds, price } = gun.ammo;

    if (chargeAndApply(player, price, () => player.giveAmmo(gun.weaponId, rounds))) {
        toast(player, `${rounds} rounds for your ${gun.name}: ${formatMoney(price)}.`, {
            tone: 'good',
        });

        pete.says(player, pick(ROUNDS_LINES), { voice: true, deal: true });
    }
}

// Pete buys back only what he sold this player, one receipt a gun, at
// BUYBACK_SHARE of his price: buying to sell always loses, and a gun found,
// looted or issued by a job earns nothing here. The gun goes before the money.
function sellGun(player: Player, gun: Gun): void {
    if (!player.hasWeapon(gun.weaponId)) {
        return;
    }

    if (receiptsFor(receipts(player), gun.weaponId) <= 0) {
        pete.says(player, 'That piece never hung on my wall. Take it somewhere else.');

        return;
    }

    const value = buyback(gun);

    if (value <= 0 || !player.removeWeapon(gun.weaponId)) {
        return;
    }

    receipt(player, gun, -1);
    player.giveMoney(value);
    toast(player, `Sold your ${gun.name} for ${formatMoney(value)}.`, { tone: 'good' });
    pete.says(player, pick(BOUGHT_BACK_LINES), { voice: true, deal: true });
}

// Pete's lines are the player's own: the card they chose from must still be
// what Pete offers them now (a gun picked up turns its line into rounds),
// else they get the new card and nothing changes hands.
function atPete(player: Player, offerId: unknown): void {
    const deal = typeof offerId === 'string' ? dealFor(offerId) : null;

    if (!deal || !peteLines(player).some((line) => line.id === offerId)) {
        shopState(player);

        return;
    }

    if (!pete.serving()) {
        toast(player, 'Pete is not at his counter right now.', { tone: 'bad' });

        return;
    }

    const refused = refusal(life.activity(player)?.kind);

    if (refused) {
        pete.says(player, refused);

        return;
    }

    if (deal.kind === 'gun') {
        buyGun(player, deal.gun);
    } else if (deal.kind === 'ammo') {
        buyRounds(player, deal.gun);
    } else {
        sellGun(player, deal.gun);
    }

    shopState(player);
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

    const shop = shopsHere().find((entry) => entry.id === shopId);

    // The Dispensary inside the wards serves only there; the street shops
    // keep serving whoever reaches their counter.
    if (
        !shop ||
        distance(player.position, shop.position) > SHOP_REACH ||
        (shopWorld(shop) !== MAIN_WORLD && player.virtualWorld !== shopWorld(shop))
    ) {
        return;
    }

    if (purchases.take(player) > 0) {
        return;
    }

    if (shop.id === PETE.id) {
        atPete(player, offerId);

        return;
    }

    const offer = shop.offers.find((entry) => entry.id === offerId);

    if (!offer) {
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

// What a customer near Pete carries changes without a deal (a gun picked up,
// dropped, emptied): their card follows.
every(PETE_CARD_MS, () => {
    if (!shopsOpen()) {
        return;
    }

    for (const player of World.getPlayers()) {
        if (
            onStreet(player) &&
            distance(player.position, PETE.position) <= PETE_CARD_RANGE &&
            peteCards.changed(player, JSON.stringify(peteLines(player)))
        ) {
            shopState(player);
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
