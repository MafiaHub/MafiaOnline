// What each player sees of the distillery: the card with the next step and
// the operation's stock, the compass and the markers. Nothing marks the camp
// on the map until a player learns of it. Once the story is done, the
// compass and objectives help only while the player is at it.
import * as profiles from '../lib/profiles';
import { card, compass, markers, toast, type Marker, type VehicleMarker } from '../lib/notify';
import { PlayerMap, PlayerSet } from '../lib/players';
import { flatDistance, formatMinutes, formatMoney, onStreet, type Point } from '../lib/util';
import { carriedLoad, carriesStolen, outletsFor, priceOf } from './cargo';
import { agedCask, nextAged } from './casks';
import { carrying, truckId, truckVehicle } from './copper';
import { heatWord } from './heat';
import {
    available,
    knows,
    MAX_CASKS,
    MAX_MOLASSES,
    opKeyOf,
    opOf,
    ops,
    QUEST,
    QUESTS,
    quality,
    RUM,
    stageOf,
    TITLE,
    type Operation,
} from './operations';
import { partyOn, partyUntil, weddingAccepted, weddingOf } from './orders';
import {
    camp,
    CASKS,
    driving,
    NEAR_CAMP,
    POT,
    SALIERI_DOOR,
    TRUCK_SPOT,
    VILLAGE,
    YARD,
} from './places';
import { revenueCall } from './raids';
import { batchOf, stillBatch, wrecked } from './still';

const COMPASS_PRIORITY = 46;
const ENGAGED_MS = 10 * 60_000;

type Item = Marker | VehicleMarker;

interface Shown {
    markers: string | null;
    compass: string | null;
    card: boolean;
}

interface Goal {
    target: Point | null;
    items: Item[];
}

const shown = new PlayerMap<Shown>({ clearOnMissionChange: true });
// When each player was last near the camp; the compass keeps helping a while after.
const lastAtCamp = new PlayerMap<number>();
// Players whose compass /rum pointed at Salieri's.
const pointed = new PlayerSet();
// Players who put the quest aside with /rum hide: no card, compass or markers
// until /rum, unless they carry something.
const hidden = new PlayerSet();

export function unpoint(player: Player): void {
    pointed.delete(player.id);
}

/** A fresh life: everything is sent again. */
export function forgetShown(player: Player): void {
    shown.delete(player.id);
}

export function nextStep(player: Player, stage: number, op: Operation | null): string {
    if (stage < QUEST.FIRST_FIRE || !op) {
        if (stage === QUEST.COPPER && carrying(player)) {
            return 'Bring the copper worm to Hank at the still';
        }

        return QUESTS[Math.min(stage, QUESTS.length - 1)].step;
    }

    const batch = stillBatch();
    const opKey = opKeyOf(player);

    if (batchOf(player)) {
        return 'Tap G to vent: keep the gauge between 45% and 80%';
    }

    if (revenueCall(opKey)) {
        return "The revenue men are coming: defend the still, or /rum bribe at Salieri's";
    }

    if (batch && batch.operatorId === null && batch.opKey === opKey) {
        return 'Your batch is banked: press G at the still';
    }

    if (carriesStolen(player)) {
        return 'Get the stolen cases into a car';
    }

    const entry = carriedLoad(player);

    if (entry?.kind === 'molasses') {
        return 'Drive the molasses to the clearing and stop by the shed';
    }

    if (entry?.kind === 'rum') {
        return weddingAccepted(entry.opKey)
            ? 'Bring 4 cases to the wedding in the village'
            : 'Drive the rum to a buyer and stop there';
    }

    if (agedCask(op) >= 0) {
        return 'A cask is ready: hold G at the casks to bottle it';
    }

    if (op.cases.n) {
        return stage === QUEST.FIRST_SALE
            ? QUESTS[QUEST.FIRST_SALE].step
            : 'Stop a car by the shed to load the cases';
    }

    if (wrecked()) {
        return 'The still is wrecked: patch it or wait for Hank';
    }

    if (op.molasses && op.casks.length < MAX_CASKS) {
        return 'Press G at the still to fire it up';
    }

    if (op.casks.length) {
        return 'The rum is ageing in the casks';
    }

    return 'Fetch molasses: stop a car at the village store and press G';
}

function cardRows(player: Player, stage: number, op: Operation | null): [string, string][] {
    const rows: [string, string][] = [];

    if (stage < QUEST.DONE) {
        rows.push([
            'Quest',
            `${stage + 1}/5 · ${QUESTS[Math.min(stage, QUESTS.length - 1)].title}`,
        ]);
    }

    if (stage < QUEST.FIRST_FIRE || !op) {
        return rows;
    }

    const batch = batchOf(player);

    if (batch) {
        rows.push([
            'Batch',
            `${Math.round(batch.progress * 100)}% · ${Math.round((batch.goodMs / Math.max(1, batch.totalMs)) * 100)}% in the green`,
        ]);
    }

    rows.push(['Molasses', `${op.molasses}/${MAX_MOLASSES} sacks`]);

    if (op.casks.length) {
        const ready = op.casks.filter((cask) => cask.left <= 0).length;
        const ageing = op.casks.filter((cask) => cask.left > 0).length;
        const next = nextAged(op);

        rows.push([
            'Casks',
            [
                ready ? `${ready} ready` : null,
                ageing && next !== null ? `${ageing} ageing · ${formatMinutes(next)}` : null,
            ]
                .filter(Boolean)
                .join(' · '),
        ]);
    }

    if (op.cases.n) {
        rows.push(['Cases', `${op.cases.n} · quality ${quality(op.cases)}%`]);
    }

    rows.push(['Heat', `${Math.round(op.heat)}% · ${heatWord(op.heat)}`]);
    const pending = revenueCall(opKeyOf(player));

    if (pending) {
        rows.push([
            'Revenue men',
            pending.pausedAt
                ? 'waiting for you'
                : `in ${formatMinutes(Math.max(0, pending.dueAt - Date.now()))} · defend or bribe`,
        ]);
    }

    const offer = weddingOf(opKeyOf(player));

    if (offer) {
        rows.push([
            'Wedding',
            `${offer.state === 'accepted' ? 'deliver' : 'offered'} · ${formatMinutes(offer.until - Date.now())}`,
        ]);
    }

    if (partyOn()) {
        rows.push(["Salieri's", `party night · ${formatMinutes(partyUntil() - Date.now())}`]);
    }

    return rows;
}

function stillItem(label: string): Marker {
    return {
        id: 'still',
        position: POT,
        label,
        color: RUM,
        model: 'sipka.i3d',
        range: 40,
        near: 25,
        map: false,
    };
}

function yardItem(): Marker {
    return {
        id: 'yard',
        position: YARD,
        label: 'STOP A CAR HERE',
        color: RUM,
        range: 80,
        near: 30,
        map: false,
    };
}

function objective(player: Player, stage: number, op: Operation | null): Goal {
    const vehicle = driving(player);
    const entry = carriedLoad(player);

    if (vehicle && entry?.kind === 'rum') {
        const wedding = weddingAccepted(entry.opKey) && opKeyOf(player) === entry.opKey;
        const owner = entry.opKey === null ? undefined : ops.get(entry.opKey);
        const places = wedding
            ? [{ id: 'wedding', position: VILLAGE, label: 'THE WEDDING', mapLabel: 'Wedding' }]
            : outletsFor(player, entry).map((outlet) => ({
                  id: outlet.id,
                  position: outlet.position,
                  label: `${outlet.label} · ${formatMoney(priceOf(outlet, entry, owner) * entry.n)}`,
                  mapLabel: outlet.mapLabel,
              }));
        const nearest = places
            .map((place) => ({ place, gap: flatDistance(place.position, vehicle.position) }))
            .sort((a, b) => a.gap - b.gap)[0]?.place;

        return {
            target: nearest?.position ?? null,
            items: places.map((place) => ({
                id: place.id,
                position: place.position,
                label: place.label,
                color: RUM,
                primary: true,
                map: { icon: 'diamond', label: place.mapLabel, edge: true },
            })),
        };
    }

    if (entry?.kind === 'molasses') {
        return { target: YARD, items: [yardItem()] };
    }

    if (stage === QUEST.ASK) {
        return { target: pointed.has(player.id) ? SALIERI_DOOR : null, items: [] };
    }

    if (stage === QUEST.FIND) {
        return { target: POT, items: [] };
    }

    if (stage === QUEST.COPPER) {
        if (carrying(player)) {
            return { target: POT, items: [stillItem('HANK')] };
        }

        const vehicleId = truckId();

        return {
            target: truckVehicle()?.position ?? TRUCK_SPOT,
            items:
                vehicleId === null
                    ? []
                    : [
                          {
                              id: 'truck',
                              vehicleId,
                              label: 'COPPER WORM',
                              color: RUM,
                              near: 60,
                              map: { icon: 'diamond', label: 'Hardware truck', edge: true },
                          },
                      ],
        };
    }

    const items: Item[] = [];

    if (!op || carriesStolen(player)) {
        return { target: null, items };
    }

    const inCamp = camp(player);
    let target: Point | null = null;

    if (!op.molasses && !op.casks.length && !op.cases.n && !stillBatch()) {
        target = VILLAGE;
        items.push({
            id: 'store',
            position: VILLAGE,
            label: 'MOLASSES',
            hint: 'STOP A CAR · PRESS G',
            color: RUM,
            near: 60,
            map: { icon: 'diamond', label: 'Molasses', edge: true },
        });
    } else if (!inCamp) {
        target = op.cases.n ? YARD : POT;
    }

    if (inCamp && !batchOf(player)) {
        if (agedCask(op) >= 0) {
            items.push({
                id: 'casks',
                position: CASKS,
                label: 'CASKS',
                color: RUM,
                model: 'sipka.i3d',
                range: 40,
                near: 25,
                map: false,
            });
        } else if (op.molasses && !stillBatch() && !wrecked() && op.casks.length < MAX_CASKS) {
            items.push(stillItem('STILL'));
        }

        if (op.cases.n && !driving(player)) {
            items.push(yardItem());
        }
    }

    return { target, items };
}

export function present(player: Player, force = false): void {
    const last = shown.get(player.id) ?? { markers: null, compass: null, card: false };

    if (force) {
        last.markers = null;
        last.compass = null;
    }

    const active = available() && onStreet(player) && Boolean(profiles.keyOf(player));
    const stage = active ? stageOf(player) : QUEST.ASK;
    const op = active ? opOf(player) : null;
    const goal: Goal = active ? objective(player, stage, op) : { target: null, items: [] };
    const aboard = Boolean(carriedLoad(player)) || carrying(player) || carriesStolen(player);

    if (active && flatDistance(player.position, POT) <= NEAR_CAMP) {
        lastAtCamp.set(player.id, Date.now());
    }

    // Once the story is done the compass and objectives help only while the
    // player is at it: near the camp lately, or with something on board.
    const engaged =
        aboard ||
        (!hidden.has(player.id) &&
            (stage < QUEST.DONE || Date.now() - (lastAtCamp.get(player.id) ?? 0) < ENGAGED_MS));

    if (!engaged) {
        goal.target = null;
        goal.items = [];
    }

    if (active && knows(player)) {
        goal.items.push({
            id: 'camp',
            position: POT,
            color: RUM,
            range: 1,
            map: { icon: 'square', label: "Hank's still", size: 5 },
        });
    }

    const payload = JSON.stringify(goal.items);

    if (payload !== last.markers) {
        last.markers = payload;
        markers(player, 'rum', goal.items);
    }

    const aim = JSON.stringify(goal.target);

    if (aim !== last.compass) {
        last.compass = aim;
        compass(player, 'rum', COMPASS_PRIORITY, goal.target ? { position: goal.target } : null);
    }

    const opKey = opKeyOf(player);
    const relevant =
        active &&
        stage > QUEST.ASK &&
        (engaged || weddingOf(opKey) !== null || revenueCall(opKey) !== null);

    if (relevant) {
        card(player, 'rum', {
            title: TITLE,
            subtitle: nextStep(player, stage, op),
            rows: cardRows(player, stage, op),
            tone: 'gold',
            progress: batchOf(player)?.progress,
        });

        last.card = true;
    } else if (last.card || force) {
        card(player, 'rum', null);
        last.card = false;
    }

    shown.set(player.id, last);
}

/** /rum: points the way to the next step (or, before the story starts, to Salieri's). */
export function guide(player: Player): void {
    const stage = stageOf(player);

    hidden.delete(player.id);

    if (stage === QUEST.ASK) {
        if (pointed.delete(player.id)) {
            present(player);

            return;
        }

        pointed.add(player.id);
        present(player);
        toast(
            player,
            "Word is somebody cooks real Cuban rum out in the woods. Ernesto, the sailor who drinks in Salieri's billiard room, might know. " +
                "Your compass points to Salieri's.",
            { title: TITLE, tone: 'gold', seconds: 9 },
        );

        return;
    }

    const last = shown.get(player.id);

    if (last) {
        last.compass = null;
    }

    present(player);
    toast(player, `${nextStep(player, stage, opOf(player))}. Your compass shows the way.`, {
        title: TITLE,
        tone: 'gold',
        seconds: 7,
    });
}

/** /rum hide. */
export function hide(player: Player): void {
    hidden.add(player.id);
    pointed.delete(player.id);
    present(player);
    toast(player, 'You put the rum business aside. /rum (or F2 → Jobs) picks it up again.', {
        title: TITLE,
    });
}

export function isHidden(player: Player): boolean {
    return hidden.has(player.id);
}

/** Takes the card, the compass and (with markers) the markers off the player's HUD. */
export function clearHud(player: Player, withMarkers: boolean): void {
    card(player, 'rum', null);
    compass(player, 'rum', COMPASS_PRIORITY, null);

    if (withMarkers) {
        markers(player, 'rum', []);
    }
}
