// What the job key (G) does around the distillery, strongest first where
// several apply. Its hints sit on the prompt layer of side activities, under
// a job's own and a home's door.
import { stoppedAt } from '../lib/errands';
import { Interactions } from '../lib/interactions';
import { toast } from '../lib/notify';
import { flatDistance, formatMinutes, formatMoney, onStreet, playerById } from '../lib/util';
import {
    buyMolasses,
    inYard,
    MOLASSES_PRICE,
    SACKS_PER_RUN,
    steal,
    theftTarget,
    warnOwners,
} from './cargo';
import { agedCask, bottle, nextAged } from './casks';
import { hank, hankSays } from './characters';
import { carrying, coilTaken, deliverCoil, truckVehicle, unbolt } from './copper';
import {
    available,
    knows,
    MAX_CASES,
    MAX_CASKS,
    opKeyOf,
    opOf,
    ops,
    QUEST,
    stageOf,
    TITLE,
    type Operation,
} from './operations';
import {
    camp,
    CASKS,
    CRATES,
    DELIVER_COIL,
    driving,
    near,
    POT,
    SAILOR,
    SAILOR_REACH,
    STILL_LEAVE,
    STOP_RADIUS,
    STOPPED_SPEED,
    TOUCH,
    TRUCK_REACH,
    VILLAGE,
} from './places';
import { talkToHank, talkToSailor } from './quest';
import { doyleWaitingFor, payDoyle } from './raids';
import { batchOf, fireUp, repair, REPAIR_PRICE, stillBatch, vent, wrecked } from './still';

// How long G is held for each hold.
const COIL_MS = 4000;
const REPAIR_MS = 4000;
const BOTTLE_MS = 3000;
const STEAL_MS = 8000;

type Action =
    | { kind: 'molasses'; vehicle: Vehicle }
    | { kind: 'vent' }
    | { kind: 'sailor' }
    | { kind: 'coil' }
    | { kind: 'bribe'; price: number }
    | { kind: 'deliver' }
    | { kind: 'repair' }
    | { kind: 'still' }
    | { kind: 'casks' }
    | { kind: 'steal'; target: { opKey: string; op: Operation } }
    | { kind: 'hank' };

// What G does where the player stands, strongest first.
function choose(player: Player): Action | null {
    if (!onStreet(player)) {
        return null;
    }

    const stage = stageOf(player);
    const vehicle = driving(player);

    if (vehicle) {
        if (stage >= QUEST.FIRST_FIRE && stoppedAt(vehicle, VILLAGE, STOP_RADIUS, STOPPED_SPEED)) {
            return { kind: 'molasses', vehicle };
        }

        return null;
    }

    if (player.getVehicle()) {
        return null;
    }

    // Tending a batch, G vents it anywhere the operator may stand: nothing
    // else near the still (Doyle's bribe, the casks) takes the press.
    if (batchOf(player) && near(player, POT, STILL_LEAVE)) {
        return { kind: 'vent' };
    }

    if (near(player, SAILOR, SAILOR_REACH, 1.5)) {
        return { kind: 'sailor' };
    }

    const truckCar = truckVehicle();

    if (
        stage === QUEST.COPPER &&
        truckCar &&
        !carrying(player) &&
        flatDistance(player.position, truckCar.position) <= TRUCK_REACH
    ) {
        return { kind: 'coil' };
    }

    if (!camp(player)) {
        return null;
    }

    const price = doyleWaitingFor(player);

    if (price !== null) {
        return { kind: 'bribe', price };
    }

    if (
        stage === QUEST.COPPER &&
        carrying(player) &&
        flatDistance(player.position, POT) <= DELIVER_COIL
    ) {
        return { kind: 'deliver' };
    }

    if (near(player, POT, TOUCH)) {
        return wrecked() ? { kind: 'repair' } : { kind: 'still' };
    }

    if (stage >= QUEST.FIRST_FIRE && near(player, CASKS, TOUCH)) {
        return { kind: 'casks' };
    }

    if (knows(player) && near(player, CRATES, TOUCH)) {
        const target = theftTarget(player);

        if (target) {
            return { kind: 'steal', target };
        }
    }

    const hankNpc = hank.present();

    if (hankNpc && flatDistance(player.position, hankNpc.position) <= TOUCH + 0.5) {
        return { kind: 'hank' };
    }

    return null;
}

export function stillHint(player: Player): string {
    const op = opOf(player);
    const stage = stageOf(player);
    const batch = stillBatch();

    if (stage < QUEST.FIRST_FIRE || !op) {
        return knows(player) ? "HANK'S STILL · IT NEEDS A COPPER WORM" : "SOMEBODY'S STILL";
    }

    if (batch && batch.operatorId !== null) {
        return `IN USE · ${(playerById(batch.operatorId)?.nickname ?? 'SOMEONE').toUpperCase()}`;
    }

    if (batch && batch.opKey !== opKeyOf(player)) {
        const owner = batch.opKey === null ? undefined : ops.get(batch.opKey);

        return `${(owner?.name ?? 'SOMEONE').toUpperCase()}'S BATCH IS BANKED`;
    }

    if (batch) {
        return '[G] TAKE OVER THE STILL';
    }

    if (op.casks.length >= MAX_CASKS) {
        return 'THE CASKS ARE FULL · BOTTLE ONE FIRST';
    }

    if (!op.molasses) {
        return 'NO MOLASSES · BUY SOME AT THE VILLAGE STORE';
    }

    return `[G] FIRE UP THE STILL · 1 OF ${op.molasses} SACKS`;
}

function hint(player: Player, action: Action): string | null {
    switch (action.kind) {
        case 'molasses':
            return `[G] BUY MOLASSES · ${SACKS_PER_RUN} SACKS · ${formatMoney(MOLASSES_PRICE)}`;
        case 'sailor':
            return '[G] TALK TO THE SAILOR';
        case 'coil':
            return coilTaken()
                ? 'THE WORM IS GONE · THE DEALER RESTOCKS SOON'
                : '[G] HOLD TO UNBOLT THE COPPER WORM';
        case 'bribe':
            return `[G] PAY DOYLE ${formatMoney(action.price)}`;
        case 'deliver':
            return '[G] GIVE HANK THE COPPER WORM';
        case 'repair':
            return `[G] HOLD · PATCH THE STILL · ${formatMoney(REPAIR_PRICE)}`;
        case 'vent':
            return null;
        case 'still':
            return stillHint(player);
        case 'casks': {
            const op = opOf(player);

            if (!op) {
                return null;
            }

            if (agedCask(op) >= 0) {
                return op.cases.n >= MAX_CASES
                    ? 'THE SHED IS FULL · LOAD A CAR FIRST'
                    : '[G] HOLD TO BOTTLE A CASK';
            }

            const next = nextAged(op);

            return next === null ? null : `AGEING · READY IN ${formatMinutes(next)}`;
        }

        case 'steal':
            return `[G] HOLD TO STEAL ${action.target.op.name.toUpperCase()}'S RUM`;
        case 'hank':
            return '[G] TALK TO HANK';
    }
}

function pressStill(player: Player): void {
    const op = opOf(player);

    if (stageOf(player) < QUEST.FIRST_FIRE || !op) {
        if (knows(player)) {
            hankSays(player, 'Not without a copper worm, friend. The village has what we need.');
        } else {
            toast(
                player,
                "Somebody runs a still out here. Whoever it is, they're not around. Maybe someone in town knows.",
                { title: TITLE },
            );
        }

        return;
    }

    const batch = stillBatch();

    if (batch && (batch.operatorId !== null || batch.opKey !== opKeyOf(player))) {
        return;
    }

    if (!batch && (op.casks.length >= MAX_CASKS || !op.molasses)) {
        const line = stillHint(player).toLowerCase();

        toast(player, `${line.replace(/^\w/, (letter) => letter.toUpperCase())}.`, {
            title: TITLE,
            tone: 'bad',
            seconds: 4,
        });

        return;
    }

    fireUp(player, op);
}

function press(player: Player, action: Action): void {
    switch (action.kind) {
        case 'molasses':
            buyMolasses(player, action.vehicle);
            break;
        case 'sailor':
            talkToSailor(player);
            break;
        case 'coil':
            if (!coilTaken()) {
                interactions.hold(player, action);
            }

            break;
        case 'bribe':
            payDoyle(player);
            break;
        case 'deliver':
            deliverCoil(player);
            break;
        case 'repair':
            interactions.hold(player, action);
            break;
        case 'vent':
            vent(player);
            break;
        case 'still':
            pressStill(player);
            break;
        case 'casks': {
            const op = opOf(player);

            if (op && agedCask(op) >= 0 && op.cases.n < MAX_CASES) {
                interactions.hold(player, action);
            }

            break;
        }

        case 'steal':
            interactions.hold(player, action);
            warnOwners(action.target.opKey);
            break;
        case 'hank':
            talkToHank(player);
            break;
    }
}

function finish(player: Player, action: Action): void {
    switch (action.kind) {
        case 'coil':
            unbolt(player);
            break;
        case 'repair':
            if (!wrecked()) {
                return;
            }

            if (!player.trySpendMoney(REPAIR_PRICE)) {
                toast(player, `The parts cost ${formatMoney(REPAIR_PRICE)}.`, {
                    title: TITLE,
                    tone: 'bad',
                });

                return;
            }

            repair();
            hankSays(player, 'Good as new. Mind the gauge this time.');
            break;
        case 'casks': {
            const op = opOf(player);

            if (op) {
                bottle(player, op);
            }

            break;
        }

        case 'steal':
            steal(player, action.target.opKey);
            break;
        default:
            break;
    }
}

export const interactions = new Interactions<Action>({
    layer: 'side',
    enabled: available,
    choose,
    hint,
    press,
    holds: {
        coil: { ms: COIL_MS, text: 'UNBOLTING THE COPPER WORM…' },
        repair: { ms: REPAIR_MS, text: 'PATCHING THE STILL…' },
        casks: { ms: BOTTLE_MS, text: 'BOTTLING…' },
        steal: { ms: STEAL_MS, text: 'STEALING CASES…' },
    },
    finish,
    busy: (player) => inYard(player) || batchOf(player) !== null,
});
