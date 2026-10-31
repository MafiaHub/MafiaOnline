// The cured tobacco on the road. It goes onto a farm truck stopped at the
// loading bay and is sold in the village, or exported at the container docks
// for far more; near the docks the whole city learns of the load, and
// whoever drives that truck in is paid.
import * as jobkey from '../lib/jobkey';
import * as profiles from '../lib/profiles';
import { roughPosition } from '../lib/stations';
import {
    toast,
    toastAll,
    markers,
    markersAll,
    feed,
    name,
    text,
    COLORS,
    type Marker,
} from '../lib/notify';
import { MAIN_WORLD, flatDistance, formatMoney, playable, plural, vehicleById } from '../lib/util';
import { stoppedAt } from '../lib/errands';
import { reward } from '../lib/rewards';
import { hangAtBarn } from './curing';
import { crewTag, mine, notice, ownerOnline, TITLE } from './owners';
import { farmers, station } from './duty';
import { priceOf, quality, type Buyer } from './market';
import { LEAF, buyers, siteNow } from './sites';
import { cargo, changed, loading, stocks, unload, type Load, type Stock } from './state';

const LOAD_RADIUS = 9;
const LOAD_SPEED = 1.5;
const LOAD_MS = 6000;
const STOP_RADIUS = 16;
const STOP_SPEED = 2;
const RUMOUR_RADIUS = 900;
const XP_PER_BUNDLE = 10;
const CARGO_GROUP = 'farm:cargo';

let lastCargoMarkers = '';
let cargoItems: Marker[] = [];

function presentCargo(force = false): void {
    // The rough position shows the truck on the map of everyone it is not
    // streamed to; the city-wide rumour is the point of the flashing blip.
    const items: Marker[] = [];

    for (const [vehicleId, entry] of cargo) {
        const vehicle = entry.rumoured ? vehicleById(vehicleId) : null;

        if (vehicle) {
            items.push({
                id: `c${vehicleId}`,
                vehicleId,
                position: roughPosition(vehicle),
                label: 'TOBACCO',
                color: COLORS.red,
                range: 300,
                near: 80,
                map: { icon: 'triangle', label: 'Tobacco truck', size: 6, flash: true },
            });
        }
    }

    const payload = JSON.stringify(items);

    if (!force && payload === lastCargoMarkers) {
        return;
    }

    lastCargoMarkers = payload;
    cargoItems = items;
    markersAll(CARGO_GROUP, items);
}

/** A player who just spawned sees the trucks the city knows of. */
export function showCargoTo(player: Player): void {
    if (cargoItems.length) {
        markers(player, CARGO_GROUP, cargoItems);
    }
}

/** The mission's trucks went with it. */
export function forgetCargoMarkers(): void {
    lastCargoMarkers = '';
    cargoItems = [];
}

function sell(driver: Player, vehicle: Vehicle, entry: Load, buyer: Buyer): void {
    cargo.delete(vehicle.id);
    changed();
    const each = priceOf(entry, buyer);
    const total = each * entry.n;
    const own = mine(driver, entry);

    reward(driver, {
        money: total,
        counters: [['tobacco', entry.n]],
        xp: XP_PER_BUNDLE * entry.n,
        reason: 'Sold tobacco',
        feed: {
            kind: 'farm',
            parts: own
                ? [
                      name(driver, LEAF),
                      text(` sold ${plural(entry.n, 'bundle')} of tobacco at ${buyer.name} · `),
                      text(formatMoney(total), COLORS.gold),
                  ]
                : [
                      name(driver, COLORS.red),
                      text(` sold ${entry.ownerName}'s tobacco at ${buyer.name} · `),
                      text(formatMoney(total), COLORS.gold),
                  ],
        },
    });

    const farmer = farmers.get(driver);

    if (farmer) {
        farmer.earned += total;
    }

    if (own) {
        toast(
            driver,
            `Sold at ${buyer.name}: ${formatMoney(each)} a bundle, ${formatMoney(total)} for ${entry.n} (quality ${quality(entry)}%).`,
            { title: TITLE, tone: 'good', seconds: 7 },
        );
    } else {
        toast(
            driver,
            `${entry.ownerName}'s tobacco fetches ${formatMoney(total)}. They will not be pleased.`,
            {
                title: 'Tobacco',
                tone: 'good',
                seconds: 7,
            },
        );

        const owner = ownerOnline(entry.ownerKey);

        if (owner) {
            toast(
                owner,
                `${driver.nickname} took your truck and sold your tobacco at ${buyer.name}.`,
                {
                    title: TITLE,
                    tone: 'bad',
                    seconds: 8,
                },
            );
        }
    }

    presentCargo();
}

// One load: towed back, wrecked, heard of near the docks, or sold. True when
// the markers change.
function tickLoad(vehicleId: number, entry: Load): boolean {
    const vehicle = vehicleById(vehicleId);

    if (!vehicle) {
        // Towed back from where it was left (lib/stations.ts): the load goes
        // back into the barn.
        cargo.delete(vehicleId);
        unload(entry);
        notice(
            entry.ownerKey,
            'towed',
            'Your loaded truck was left standing, so it was towed back and unloaded at the barn.',
        );

        return true;
    }

    if (vehicle.terminalState !== 0) {
        cargo.delete(vehicleId);
        changed();
        notice(
            entry.ownerKey,
            'lost',
            'The farm truck carrying your tobacco is wrecked. The load is lost.',
            'bad',
        );

        feed('farm', [
            text('Tobacco', LEAF),
            text(` · ${entry.ownerName}'s load went up with its truck`),
        ]);

        return true;
    }

    let rumoured = false;

    if (
        !entry.rumoured &&
        buyers().some(
            (buyer) =>
                buyer.risky && flatDistance(vehicle.position, buyer.position) <= RUMOUR_RADIUS,
        )
    ) {
        entry.rumoured = rumoured = true;
        const driver = vehicle.getDriver();

        toastAll(
            'A farm truck loaded with tobacco is heading for the docks. Whoever drives it in gets paid for the load.',
            { title: 'Tobacco', tone: 'gold', seconds: 7 },
        );

        feed('farm', [
            text('Tobacco', LEAF),
            text(' · a loaded farm truck is heading for the docks'),
            ...(driver ? [text(' · '), name(driver, COLORS.white)] : []),
        ]);
    }

    const driver = vehicle.getDriver();

    if (!driver || !playable(driver)) {
        return rumoured;
    }

    const buyer = buyers().find((candidate) =>
        stoppedAt(vehicle, candidate.position, STOP_RADIUS, STOP_SPEED),
    );

    if (buyer) {
        sell(driver, vehicle, entry, buyer);
    }

    return rumoured;
}

export function tickCargo(): void {
    let changedMarkers = false;

    for (const [vehicleId, entry] of [...cargo]) {
        changedMarkers = tickLoad(vehicleId, entry) || changedMarkers;
    }

    // Unchanged items are not resent; a rumoured truck's rough position moves.
    presentCargo(changedMarkers);
}

function refuse(player: Player, vehicle: Vehicle, message: string): void {
    if (loading.get(player.id)?.refused) {
        return;
    }

    loading.set(player.id, { vehicleId: vehicle.id, refused: true });
    toast(player, message, { title: TITLE, tone: 'bad' });
}

// Cured tobacco goes onto a farm truck held still at the loading bay.
function tickLoading(player: Player, key: string, stock: Stock, now: number): void {
    const vehicle = player.getSeat() === 0 ? player.getVehicle() : null;
    const pending = loading.get(player.id);
    const atBay =
        vehicle !== null &&
        stock.cured.n > 0 &&
        stoppedAt(vehicle, siteNow().loading, LOAD_RADIUS, LOAD_SPEED);

    if (!atBay) {
        if (pending) {
            loading.delete(player.id);
            jobkey.prompt(player, null);

            if (!pending.refused && vehicle && stock.cured.n > 0) {
                toast(player, 'You pulled away before the tobacco was loaded.', {
                    title: TITLE,
                    tone: 'bad',
                });
            }
        }

        return;
    }

    if (!station.isVehicle(vehicle)) {
        refuse(
            player,
            vehicle,
            "The bales go on a farm truck: bring one of the farm's two trucks.",
        );

        return;
    }

    const aboard = cargo.get(vehicle.id);

    if (aboard && aboard.ownerKey !== key) {
        refuse(player, vehicle, `This truck already carries ${aboard.ownerName}'s tobacco.`);

        return;
    }

    if (!pending || pending.refused || pending.vehicleId !== vehicle.id) {
        loading.set(player.id, { vehicleId: vehicle.id, startedAt: now });

        return;
    }

    const progress = Math.min(1, (now - pending.startedAt) / LOAD_MS);

    jobkey.prompt(player, 'LOADING THE TOBACCO… HOLD STILL', progress);

    if (progress < 1) {
        return;
    }

    loading.delete(player.id);
    jobkey.prompt(player, null);
    const entry: Load = aboard ?? {
        ownerKey: key,
        ownerName: player.nickname,
        ownerCrew: crewTag(player),
        n: 0,
        q: 0,
        rumoured: false,
    };

    entry.n += stock.cured.n;
    entry.q += stock.cured.q;
    cargo.set(vehicle.id, entry);
    stock.cured = { n: 0, q: 0 };
    changed();
    const offers = buyers()
        .map((buyer) => `${formatMoney(priceOf(entry, buyer))} a bundle at ${buyer.name}`)
        .join(', ');

    toast(
        player,
        `${plural(entry.n, 'bundle')} loaded: ${offers}.` +
            (buyers().some((buyer) => buyer.risky)
                ? ' Near the docks the whole city will know, and anyone can take the truck.'
                : ''),
        { title: TITLE, tone: 'gold', seconds: 10 },
    );
}

// Leaves are hung up by walking (or driving) to the barn's arrow; cured
// tobacco goes onto a farm truck stopped at the loading bay.
export function tickBarn(now: number): void {
    for (const player of World.getPlayers()) {
        const key = profiles.keyOf(player);
        const stock = key ? stocks.get(key) : undefined;

        if (!key || !stock || !playable(player) || player.virtualWorld !== MAIN_WORLD) {
            loading.delete(player.id);
            continue;
        }

        hangAtBarn(player, stock);
        tickLoading(player, key, stock, now);
    }
}
