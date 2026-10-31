// What each player sees of the farm: a farmhand's card, compass and markers
// (the next step, the field, plants losing quality), and anyone's at the
// wheel of a loaded truck (the buyers and what they pay).
import * as profiles from '../lib/profiles';
import { card, compass, markers, COLORS, type Marker } from '../lib/notify';
import {
    MAIN_WORLD,
    flatDistance,
    formatMinutes,
    formatMoney,
    plural,
    type Point,
} from '../lib/util';
import { MAX_PLANTS, isRaining, ownPlants, plantsNear, plotPosition } from './field';
import { priceOf, quality } from './market';
import { mine } from './owners';
import { RIPE, STAGE_MS, ripe, wilting } from './plant';
import { LEAF, buyers, here, siteNow } from './sites';
import { cargo, stocks, type Load, type Plant, type Stock } from './state';

/** The id of the farm's card, compass source and markers. */
export const HUD = 'farm';
export const COMPASS_PRIORITY = 45;
const LABEL_RANGE = 25;

/** A farmhand on duty, as the card shows them. */
export interface Farmhand {
    earned: number;
    /** A farm truck's hose reaches them. */
    truckNear: boolean;
}

function nextStep(farmhand: Farmhand, stock: Stock | null, plants: Plant[]): string {
    if (plants.some(ripe)) {
        return 'Hold G at the ripe plants to pick the leaves';
    }

    if (plants.some((plant) => plant.pest)) {
        return 'Hornworms! Hold G at the plant to pick them off';
    }

    if (plants.some((plant) => plant.dry)) {
        return farmhand.truckNear
            ? 'Hold G near the dry plants to water them'
            : 'Bring a farm truck to the field: its hose waters the plants';
    }

    if (stock?.leaves.n) {
        return "Carry the leaves to the barn's arrow to cure them";
    }

    if (stock?.cured.n) {
        return 'Stop a farm truck at the loading bay by the barn';
    }

    if (plants.length < MAX_PLANTS) {
        return 'Walk into the field and press G to plant';
    }

    if (stock?.curing.length) {
        return 'Curing in the barn';
    }

    return 'Growing. Keep them watered';
}

function farmRows(farmhand: Farmhand, stock: Stock | null, plants: Plant[]): [string, string][] {
    const rows: [string, string][] = [];

    if (plants.length) {
        const ripeCount = plants.filter(ripe).length;
        const dry = plants.filter((plant) => plant.dry).length;
        const pests = plants.filter((plant) => plant.pest).length;
        const status = [
            dry ? `${dry} dry` : null,
            pests ? `${pests} hornworms` : null,
            ripeCount ? `${ripeCount} ripe` : null,
        ]
            .filter((part) => part !== null)
            .join(' · ');

        rows.push(['Your plants', `${plants.length}/${MAX_PLANTS}${status ? ` · ${status}` : ''}`]);
        const growing = plants.filter((plant) => plant.stage < RIPE && !plant.dry && !plant.pest);

        if (growing.length) {
            rows.push([
                'Next stage',
                formatMinutes(
                    Math.min(...growing.map((plant) => STAGE_MS[plant.stage] - plant.grown)),
                ),
            ]);
        }

        rows.push([
            'Quality',
            `${Math.round(plants.reduce((sum, plant) => sum + plant.quality, 0) / plants.length)}%`,
        ]);
    }

    if (stock?.leaves.n) {
        rows.push(['Leaves picked', `${stock.leaves.n} · ${quality(stock.leaves)}%`]);
    }

    if (stock?.curing.length) {
        rows.push([
            'Curing',
            `${stock.curing.reduce((sum, batch) => sum + batch.n, 0)} · ${formatMinutes(Math.min(...stock.curing.map((batch) => batch.left)))}`,
        ]);
    }

    if (stock?.cured.n) {
        rows.push(['Cured', `${stock.cured.n} ready to load`]);
    }

    if (isRaining() && plants.length) {
        rows.push(['Weather', 'rain is watering the field']);
    }

    rows.push(['This shift', formatMoney(farmhand.earned)]);

    return rows;
}

function markerItems(
    player: Player,
    farmhand: Farmhand | null,
    stock: Stock | null,
    entry: Load | null,
): Marker[] {
    const items: Marker[] = [];

    if (entry) {
        for (const buyer of buyers()) {
            items.push({
                id: buyer.id,
                position: buyer.position,
                label: `${buyer.label} · ${formatMoney(priceOf(entry, buyer) * entry.n)}`,
                color: buyer.risky ? COLORS.red : LEAF,
                primary: true,
                map: { icon: 'diamond', label: buyer.mapLabel, edge: true },
            });
        }

        return items;
    }

    const site = siteNow();

    if (stock?.cured.n) {
        items.push({
            id: 'load',
            position: site.loading,
            label: 'LOADING BAY',
            hint: 'STOP A FARM TRUCK HERE',
            color: LEAF,
            range: 300,
            near: 60,
            map: { icon: 'dot', label: 'Loading bay', size: 5 },
        });
    }

    if (!farmhand) {
        return items;
    }

    items.push({
        id: 'field',
        position: site.field.center,
        label: 'TOBACCO FIELD',
        color: LEAF,
        range: 300,
        near: 45,
        map: { icon: 'square', label: 'Tobacco field', size: 5 },
    });

    // Only plants losing quality are labelled; the rest show by their size
    // and the card counts them.
    for (const { plant } of plantsNear(player.position, LABEL_RANGE)) {
        if (!wilting(plant)) {
            continue;
        }

        const spot = plotPosition(plant.plot);

        items.push({
            id: `p${plant.plot}`,
            position: { x: spot.x, y: spot.y + 0.2, z: spot.z },
            label: plant.pest ? 'HORNWORMS' : 'WILTING',
            color: plant.pest ? COLORS.red : COLORS.blue,
            range: LABEL_RANGE,
            near: 14,
            map: false,
        });
    }

    return items;
}

function compassTarget(
    player: Player,
    farmhand: Farmhand | null,
    stock: Stock | null,
    entry: Load | null,
    plants: Plant[],
): Point | null {
    if (entry) {
        const nearest = buyers()
            .map((buyer) => ({ buyer, gap: flatDistance(buyer.position, player.position) }))
            .sort((a, b) => a.gap - b.gap)[0];

        return nearest ? nearest.buyer.position : null;
    }

    if (!farmhand) {
        return null;
    }

    const { signOn, loading, field } = siteNow();

    if (stock?.leaves.n && !plants.some(ripe)) {
        return signOn;
    }

    if (stock?.cured.n && !plants.some((plant) => ripe(plant) || plant.dry || plant.pest)) {
        return loading;
    }

    if (
        flatDistance(player.position, field.center) > 45 &&
        (!plants.length || plants.some((plant) => plant.dry || plant.pest || ripe(plant)))
    ) {
        return field.center;
    }

    return null;
}

function loadCard(player: Player, vehicle: Vehicle, entry: Load): void {
    const rows: [string, string][] = [
        ['Load', `${plural(entry.n, 'bundle')} · quality ${quality(entry)}%`],
    ];

    for (const buyer of buyers()) {
        rows.push([
            buyer.risky ? 'Docks (risky)' : 'Village co-op',
            `${Math.round(flatDistance(buyer.position, vehicle.position))} m · ${formatMoney(priceOf(entry, buyer) * entry.n)}`,
        ]);
    }

    if (entry.rumoured) {
        rows.push(['Watch out', 'the city knows about this load']);
    }

    card(player, HUD, {
        title: 'Tobacco',
        subtitle: mine(player, entry)
            ? 'Drive the load to a buyer and stop there'
            : `${entry.ownerName}'s load: sell it before they catch you`,
        rows,
        tone: 'gold',
    });
}

/**
 * The player's card, compass and markers. Sent every second; lib/notify.ts
 * sends only what changed.
 */
export function present(player: Player, farmhand: Farmhand | null): void {
    const key = profiles.keyOf(player);
    const stock = key ? (stocks.get(key) ?? null) : null;
    const vehicle = player.getSeat() === 0 ? player.getVehicle() : null;
    const active = here() && player.virtualWorld === MAIN_WORLD;
    const entry = vehicle && active ? (cargo.get(vehicle.id) ?? null) : null;
    const plants = ownPlants(key);

    markers(player, HUD, active ? markerItems(player, farmhand, stock, entry) : []);
    const target = active ? compassTarget(player, farmhand, stock, entry, plants) : null;

    compass(player, HUD, COMPASS_PRIORITY, target ? { position: target } : null);

    if (vehicle && entry) {
        loadCard(player, vehicle, entry);
    } else if (active && farmhand) {
        card(player, HUD, {
            title: 'Tobacco farm',
            subtitle: nextStep(farmhand, stock, plants),
            rows: farmRows(farmhand, stock, plants),
            tone: 'good',
        });
    } else {
        card(player, HUD, null);
    }
}
