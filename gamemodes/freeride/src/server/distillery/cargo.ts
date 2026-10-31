// Loads and buyers. Molasses comes from the village store (G in a car
// stopped out front) to the clearing; bottled cases go out in a car stopped
// in the clearing to Luigi at Salieri's (safe, pays least), the speakeasy at
// Morello's (the police may notice) or Paco at the docks (pays most, the
// city is told and anyone may drive the car in). Another player may steal
// cases from the shed while an owner is online and sell them as their own.
// A load in a car and cases in a thief's hands go back to their operation
// when the map changes or the resource stops.
import * as profiles from '../lib/profiles';
import { stoppedAt } from '../lib/errands';
import * as maps from '../lib/maps';
import { speak } from '../lib/npcs';
import {
    feed,
    markers,
    markersAll,
    name,
    text,
    toast,
    toastAll,
    COLORS,
    type VehicleMarker,
} from '../lib/notify';
import { PlayerMap } from '../lib/players';
import {
    flatDistance,
    formatMoney,
    playable,
    playerById,
    plural,
    speedOf,
    vehicleById,
    onStreet,
    type Point,
} from '../lib/util';
import { interactions } from './actions';
import { hankSays, LINE } from './characters';
import { addHeat, HEAT_NERVOUS, NERVOUS_PRICE } from './heat';
import {
    addCases,
    changed,
    cooldownOf,
    MAX_MOLASSES,
    members,
    notice,
    opKeyOf,
    opOf,
    ops,
    QUEST,
    quality,
    recordOf,
    RUM,
    setWanted,
    stageOf,
    takeCases,
    tellOp,
    TITLE,
    wantedLevel,
    XP,
    type Operation,
    type Stock,
} from './operations';
import { partyOn, PARTY_PRICE, weddingDelivery } from './orders';
import {
    driving,
    RUMOUR_RADIUS,
    STOP_RADIUS,
    STOPPED_SPEED,
    VILLAGE,
    YARD,
    YARD_RADIUS,
} from './places';

const MOLASSES_PRICE = 40;
const SACKS_PER_RUN = 3;
const CAR_CAPACITY = 12;
const LOAD_MS = 5000;
const UNLOAD_MS = 4000;
const STEAL_CASES = 4;
const THEFT_GAP_MS = 10 * 60_000;
const THEFT_TOLD_MS = 30_000;

export { MOLASSES_PRICE, SACKS_PER_RUN };

/** Cases or sacks of an operation on the move. */
export interface Lot extends Stock {
    opKey: string | null;
    opName: string;
}

/** What a car carries. */
export interface Load extends Lot {
    kind: 'rum' | 'molasses';
    /** The city knows of it: everyone sees the car. */
    rumoured: boolean;
}

export interface Outlet {
    id: 'salieri' | 'morello' | 'docks';
    who: string;
    name: string;
    label: string;
    mapLabel: string;
    position: Point;
    /** A case of the best rum. */
    price: number;
    /** Heat per case sold. */
    heat: number;
    /** The chance a patrolman sees the crates go in. */
    tail: number;
    /** The quest stage from which an owner is shown it. */
    unlock: number;
    /** A load heading there is told to the whole city. */
    rumour?: boolean;
}

// A full car (12 cases) of the best rum: $360 at Luigi's, $540 at the
// speakeasy, $720 and 72 heat at the docks.
export const OUTLETS: readonly Outlet[] = [
    {
        id: 'salieri',
        who: 'Luigi',
        name: "Luigi at Salieri's",
        label: "LUIGI · SALIERI'S",
        mapLabel: 'Luigi (rum)',
        position: { x: -1781.1, y: -5.5, z: -8.2 },
        price: 30,
        heat: 1,
        tail: 0,
        unlock: 4,
    },
    {
        id: 'morello',
        who: 'The speakeasy',
        name: "the speakeasy at Morello's",
        label: "MORELLO'S SPEAKEASY",
        mapLabel: 'Speakeasy (rum)',
        position: { x: -404.0, y: 17.0, z: 440.3 },
        price: 45,
        heat: 2,
        tail: 0.35,
        unlock: 4,
    },
    {
        id: 'docks',
        who: 'Paco',
        name: 'Paco at the docks',
        label: 'PACO · THE DOCKS',
        mapLabel: 'Paco (rum)',
        position: maps.landmark('docks').position,
        price: 60,
        heat: 6,
        tail: 0,
        unlock: 5,
        rumour: true,
    },
];

// vehicle id -> what it carries
const cargo = new Map<number, Load>();
// Cases a thief carries, not yet in a car.
const stolen = new PlayerMap<Lot>();
// Cars stopped in the clearing, loading or unloading.
const yardStops = new PlayerMap<{ vehicleId: number; startedAt: number }>({
    clearOnMissionChange: true,
});
let lastRumours = '';
let rumourItems: VehicleMarker[] = [];

export function loadOf(vehicle: Vehicle | null): Load | null {
    return vehicle ? (cargo.get(vehicle.id) ?? null) : null;
}

/** What the player drives with: a load aboard. */
export function carriedLoad(player: Player): Load | null {
    return loadOf(driving(player));
}

export function carriesStolen(player: Player): boolean {
    return stolen.has(player.id);
}

export function inYard(player: Player): boolean {
    return yardStops.has(player.id);
}

export function unload(vehicle: Vehicle): void {
    cargo.delete(vehicle.id);
}

export function priceOf(outlet: Outlet, entry: Stock, op: Operation | null | undefined): number {
    const heat = op && op.heat >= HEAT_NERVOUS ? NERVOUS_PRICE : 1;
    const boost = outlet.id === 'salieri' && partyOn() ? PARTY_PRICE : 1;

    return Math.round(outlet.price * (0.4 + (0.6 * quality(entry)) / 100) * heat * boost);
}

function opFor(opKey: string | null): Operation | undefined {
    return opKey === null ? undefined : ops.get(opKey);
}

export function presentRumours(force = false): void {
    const items: VehicleMarker[] = [...cargo]
        .filter(([, entry]) => entry.kind === 'rum' && entry.rumoured)
        .map(([vehicleId]) => ({
            id: `r${vehicleId}`,
            vehicleId,
            label: 'RUM',
            color: COLORS.red,
            range: 300,
            near: 80,
            map: { icon: 'triangle', label: 'Rum car', size: 6, flash: true },
        }));
    const payload = JSON.stringify(items);

    if (!force && payload === lastRumours) {
        return;
    }

    lastRumours = payload;
    rumourItems = items;
    markersAll('rum:cargo', items);
}

/** A fresh life sees the rum cars the city knows of. */
export function showRumours(player: Player): void {
    if (rumourItems.length) {
        markers(player, 'rum:cargo', rumourItems);
    }
}

export function forgetRumours(): void {
    lastRumours = '';
    rumourItems = [];
}

function returnToOp(entry: Lot & { kind: Load['kind'] }): void {
    const op = opFor(entry.opKey);

    if (!op) {
        return;
    }

    if (entry.kind === 'molasses') {
        op.molasses = Math.min(MAX_MOLASSES, op.molasses + entry.n);
    } else {
        addCases(op.cases, entry);
    }

    changed();
}

/** Every load and every thief's cases go back to their operations. */
export function returnLoads(): void {
    for (const entry of cargo.values()) {
        returnToOp(entry);
    }

    cargo.clear();

    for (const lot of stolen.values()) {
        returnToOp({ kind: 'rum', ...lot });
    }

    stolen.clear();
}

/** A player who joined a crew brought their loads on the road into its operation. */
export function moveLoads(from: string, to: string, opName: string): void {
    for (const entry of cargo.values()) {
        if (entry.opKey !== from) {
            continue;
        }

        entry.opKey = to;
        entry.opName = opName;
    }
}

export function buyMolasses(player: Player, vehicle: Vehicle): void {
    const op = opOf(player);

    if (!op) {
        return;
    }

    const aboard = cargo.get(vehicle.id);

    if (aboard && aboard.kind !== 'molasses') {
        toast(player, 'This car carries rum. Unload it first.', { title: TITLE, tone: 'bad' });

        return;
    }

    const carried = aboard?.n ?? 0;
    const room = MAX_MOLASSES - op.molasses - carried;

    if (room <= 0) {
        toast(player, `Hank has no room for more than ${MAX_MOLASSES} sacks.`, {
            title: TITLE,
            tone: 'bad',
        });

        return;
    }

    if (!player.trySpendMoney(MOLASSES_PRICE)) {
        toast(player, `Molasses costs ${formatMoney(MOLASSES_PRICE)} for ${SACKS_PER_RUN} sacks.`, {
            title: TITLE,
            tone: 'bad',
        });

        return;
    }

    const count = Math.min(SACKS_PER_RUN, room);

    cargo.set(vehicle.id, {
        kind: 'molasses',
        opKey: opKeyOf(player),
        opName: op.name,
        n: carried + count,
        q: 0,
        rumoured: false,
    });

    toast(
        player,
        `${plural(count, 'sack')} of molasses in the back. Drive them to the clearing and stop by the shed.`,
        { title: TITLE, tone: 'good' },
    );
}

function tellThief(owner: Load, driver: Player, outlet: Outlet, total: number): void {
    toast(
        driver,
        `${owner.opName}'s rum fetches ${formatMoney(total)}. They will not be pleased.`,
        {
            title: 'Rum',
            tone: 'good',
            seconds: 7,
        },
    );

    feed('rum', [
        name(driver, COLORS.red),
        text(` sold ${owner.opName}'s rum to ${outlet.name} · `),
        text(formatMoney(total), COLORS.gold),
    ]);

    for (const member of members(owner.opKey)) {
        toast(member, `${driver.nickname} sold your rum to ${outlet.name}.`, {
            title: TITLE,
            tone: 'bad',
            seconds: 8,
        });
    }
}

function sell(driver: Player, vehicle: Vehicle, entry: Load, outlet: Outlet): void {
    const op = opFor(entry.opKey);
    const each = priceOf(outlet, entry, op);
    const total = each * entry.n;

    cargo.delete(vehicle.id);
    changed();
    driver.giveMoney(total);
    profiles.bump(driver, 'rum', entry.n);
    profiles.addXp(driver, XP.sell * entry.n, 'Sold rum');
    const own = opKeyOf(driver) === entry.opKey;

    if (!own) {
        // Stolen rum heats up the thief's own still, if they run one.
        const thiefKey = opKeyOf(driver);

        if (opFor(thiefKey)?.unlocked) {
            addHeat(thiefKey, outlet.heat * entry.n);
        }

        tellThief(entry, driver, outlet, total);
    } else {
        addHeat(entry.opKey, outlet.heat * entry.n);

        if (op) {
            op.sold += entry.n;
        }

        toast(
            driver,
            `${outlet.who} takes ${plural(entry.n, 'case')}: ${formatMoney(each)} a case, ${formatMoney(total)} (quality ${quality(entry)}%).`,
            { title: TITLE, tone: 'good', seconds: 7 },
        );

        feed('rum', [
            name(driver, RUM),
            text(` sold ${plural(entry.n, 'case')} of rum · `),
            text(formatMoney(total), COLORS.gold),
        ]);

        if (op && op.progress === QUEST.FIRST_SALE) {
            op.progress = QUEST.DONE;

            for (const member of members(entry.opKey)) {
                const record = recordOf(member);

                if (record) {
                    record.done = true;
                }
            }

            profiles.addXp(driver, XP.lead, 'First rum sold');
            speak(
                driver,
                'Ernesto',
                'Word travels, amigo. My cousin Paco unloads banana boats at the container docks. He pays double for good rum, ' +
                    'but the whole city watches a load go down there, and anyone can take it off you.',
                { voice: LINE.chat, seconds: 12 },
            );
        }
    }

    if (outlet.tail && Math.random() < outlet.tail) {
        setWanted(driver, 1);
        toast(driver, 'A patrolman saw the crates go in. Lose him.', { title: TITLE, tone: 'bad' });
    }

    presentRumours();
}

// The buyers a load's driver is shown: its owners learn of them as the quest
// goes on; anyone else with the load knows where rum sells.
export function outletsFor(player: Player, entry: Load): Outlet[] {
    const own = opKeyOf(player) === entry.opKey;
    const stage = stageOf(player);

    return OUTLETS.filter((outlet) => !own || stage >= outlet.unlock);
}

export function tickCargo(): void {
    let moved = false;

    for (const [vehicleId, entry] of [...cargo]) {
        const vehicle = vehicleById(vehicleId);

        if (!vehicle) {
            cargo.delete(vehicleId);
            returnToOp(entry);
            moved = true;
            continue;
        }

        if (vehicle.terminalState !== 0) {
            cargo.delete(vehicleId);
            changed();
            moved = true;
            tellOp(
                entry.opKey,
                entry.kind === 'rum'
                    ? 'The car carrying your rum is wrecked. The cases are lost.'
                    : 'The car with your molasses is wrecked.',
                'bad',
            );

            if (entry.kind === 'rum') {
                feed('rum', [
                    text('Rum', RUM),
                    text(` · ${entry.opName}'s load went up with its car`),
                ]);
            }

            continue;
        }

        const driver = vehicle.getDriver();

        if (
            entry.kind === 'rum' &&
            !entry.rumoured &&
            OUTLETS.some(
                (outlet) =>
                    outlet.rumour &&
                    flatDistance(vehicle.position, outlet.position) <= RUMOUR_RADIUS,
            )
        ) {
            entry.rumoured = moved = true;
            toastAll(
                'A car loaded with Cuban rum is heading for the docks. Whoever drives it in gets paid for the load.',
                { title: 'Rum', tone: 'gold', seconds: 7 },
            );

            feed('rum', [
                text('Rum', RUM),
                text(' · a loaded car is heading for the docks'),
                ...(driver ? [text(' · '), name(driver, COLORS.white)] : []),
            ]);
        }

        if (entry.kind !== 'rum' || !driver || !playable(driver) || speedOf(vehicle) > 2) {
            continue;
        }

        const atVillage = flatDistance(vehicle.position, VILLAGE) <= STOP_RADIUS;

        if (atVillage && weddingDelivery(driver, vehicle, entry)) {
            moved = true;
            continue;
        }

        const outlet = OUTLETS.find(
            (candidate) => flatDistance(vehicle.position, candidate.position) <= STOP_RADIUS,
        );

        if (!outlet) {
            continue;
        }

        if (wantedLevel(driver) > 0) {
            notice(
                driver,
                'wanted',
                `${outlet.who} won't touch the crates with the police on your tail. Lose them first.`,
                'bad',
            );

            continue;
        }

        sell(driver, vehicle, entry, outlet);
        moved = true;
    }

    if (moved) {
        presentRumours();
    }

    // Stolen cases in a thief's hands go into the first car they drive.
    for (const [id, lot] of [...stolen]) {
        const player = playerById(id);
        const vehicle = player ? driving(player) : null;

        if (!player || !vehicle || cargo.has(vehicle.id)) {
            continue;
        }

        cargo.set(vehicle.id, { kind: 'rum', ...lot, rumoured: true });
        stolen.delete(id);
        toast(
            player,
            'The cases are in the car. Their owners can see it on the map: sell them fast.',
            {
                title: 'Rum',
                tone: 'gold',
            },
        );

        presentRumours();
    }
}

// Cars stopped in the clearing unload molasses or take on cases.
export function tickYard(now: number): void {
    for (const player of World.getPlayers()) {
        const pending = yardStops.get(player.id);
        const vehicle = onStreet(player) ? driving(player) : null;
        const op = vehicle && stageOf(player) >= QUEST.FIRST_FIRE ? opOf(player) : null;
        const opKey = op ? opKeyOf(player) : null;
        const aboard = loadOf(vehicle);
        const atShed =
            Boolean(op) && vehicle !== null && stoppedAt(vehicle, YARD, YARD_RADIUS, STOPPED_SPEED);
        const unloading = atShed && aboard?.kind === 'molasses' && aboard.opKey === opKey;
        const loading =
            atShed &&
            !unloading &&
            op !== null &&
            op.cases.n > 0 &&
            (!aboard ||
                (aboard.kind === 'rum' && aboard.opKey === opKey && aboard.n < CAR_CAPACITY));

        if (!vehicle || !op || (!unloading && !loading)) {
            if (pending) {
                yardStops.delete(player.id);
                interactions.prompt(player, null);

                if (vehicle && pending.vehicleId === vehicle.id) {
                    toast(player, 'You pulled away too soon.', {
                        title: TITLE,
                        tone: 'bad',
                        seconds: 3,
                    });
                }
            }

            continue;
        }

        if (pending?.vehicleId !== vehicle.id) {
            yardStops.set(player.id, { vehicleId: vehicle.id, startedAt: now });
            continue;
        }

        const needed = unloading ? UNLOAD_MS : LOAD_MS;
        const progress = Math.min(1, (now - pending.startedAt) / needed);

        interactions.prompt(
            player,
            unloading ? 'HANK UNLOADS THE MOLASSES… HOLD STILL' : 'LOADING THE RUM… HOLD STILL',
            progress,
        );

        if (progress < 1) {
            continue;
        }

        yardStops.delete(player.id);
        interactions.prompt(player, null);

        if (unloading && aboard) {
            op.molasses = Math.min(MAX_MOLASSES, op.molasses + aboard.n);
            cargo.delete(vehicle.id);
            changed();
            hankSays(
                player,
                `${plural(aboard.n, 'sack')} of molasses: that's ${op.molasses} ${op.molasses === 1 ? 'batch' : 'batches'} on the still. ` +
                    'Press G there to fire it up.',
            );

            continue;
        }

        const entry: Load = aboard ?? {
            kind: 'rum',
            opKey,
            opName: op.name,
            n: 0,
            q: 0,
            rumoured: false,
        };

        addCases(entry, takeCases(op.cases, Math.min(op.cases.n, CAR_CAPACITY - entry.n)));
        cargo.set(vehicle.id, entry);
        changed();
        const offers = outletsFor(player, entry)
            .map((outlet) => `${outlet.who} ${formatMoney(priceOf(outlet, entry, op))}`)
            .join(', ');

        toast(
            player,
            `${plural(entry.n, 'case')} loaded. A case pays: ${offers}. Buyers won't deal while the police are on you.`,
            { title: TITLE, tone: 'gold', seconds: 9 },
        );

        if (Math.random() < op.heat / 200) {
            setWanted(player, 1);
            toast(
                player,
                'Someone on the road saw the crates go in. The police are looking for your car.',
                { title: TITLE, tone: 'bad' },
            );
        }
    }
}

/* Theft */

/** The operation a stranger who knows the camp may take cases from: the best stocked with an owner in town. */
export function theftTarget(player: Player): { opKey: string; op: Operation } | null {
    const own = opKeyOf(player);
    let best: { opKey: string; op: Operation } | null = null;

    for (const [opKey, op] of ops) {
        if (opKey === own || op.cases.n <= 0 || !members(opKey).length) {
            continue;
        }

        if (Date.now() - cooldownOf(opKey).theftAt < THEFT_GAP_MS) {
            continue;
        }

        if (!best || op.cases.n > best.op.cases.n) {
            best = { opKey, op };
        }
    }

    return best;
}

/** The owners hear of a thief at their crates, now and then, not on every press of G. */
export function warnOwners(opKey: string): void {
    const wait = cooldownOf(opKey);

    if (Date.now() - wait.toldAt >= THEFT_TOLD_MS) {
        wait.toldAt = Date.now();
        tellOp(opKey, 'Someone is stealing your rum at the still!', 'bad', 6);
    }
}

/** The hold at the crates is done. */
export function steal(player: Player, opKey: string): void {
    const op = ops.get(opKey);

    if (!op || !op.cases.n || !members(opKey).length) {
        return;
    }

    // Another thief may have got there first.
    if (Date.now() - cooldownOf(opKey).theftAt < THEFT_GAP_MS) {
        toast(player, 'Somebody beat you to it: the rest is watched now.', {
            title: 'Rum',
            tone: 'bad',
        });

        return;
    }

    const lot = takeCases(op.cases, Math.min(STEAL_CASES, op.cases.n));

    changed();
    cooldownOf(opKey).theftAt = Date.now();
    stolen.set(player.id, { opKey, opName: op.name, ...lot });
    toast(
        player,
        `You carry off ${plural(lot.n, 'case')}. Get into a car: they go in the first one you drive.`,
        { title: 'Rum', tone: 'gold' },
    );

    tellOp(
        opKey,
        `${player.nickname} stole ${plural(lot.n, 'case')} of your rum! Their car will show on the map.`,
        'bad',
        8,
    );
}

/** The thief went down: the cases smashed. */
export function dropStolen(player: Player): boolean {
    return stolen.delete(player.id);
}

/** A thief who leaves gives the cases back to their owners. */
export function returnStolen(player: Player): void {
    const lot = stolen.get(player.id);

    if (lot) {
        returnToOp({ kind: 'rum', ...lot });
    }

    stolen.delete(player.id);
}

export function dropYardStop(player: Player): void {
    yardStops.delete(player.id);
}
