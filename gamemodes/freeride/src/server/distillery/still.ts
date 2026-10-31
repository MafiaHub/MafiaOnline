// The still. A sack of molasses makes a batch; the operator keeps its
// pressure gauge in the green by tapping G to vent. Low pressure crawls,
// past the band it whistles, and at the top the pot blows: the batch is
// lost, the valley hears it and the still stands wrecked until Hank patches
// it (or someone pays for the parts). The share of time spent in the green
// is the rum's quality. An operator who walks off banks the fire; the batch
// waits a while for someone of theirs to carry on.
import * as profiles from '../lib/profiles';
import { feed, name, text, toast, COLORS } from '../lib/notify';
import { PlacedObjects, type ObjectSpec } from '../lib/placed_objects';
import {
    distance,
    emit,
    formatMinutes,
    formatMoney,
    MAIN_WORLD,
    onStreet,
    playerById,
} from '../lib/util';
import { CASK_AGE_MS } from './casks';
import { hank, hankSays, LINE } from './characters';
import { addHeat, HEAT_BATCH, HEAT_BLAST } from './heat';
import { interactions } from './actions';
import {
    changed,
    MAX_MOLASSES,
    opKeyOf,
    ops,
    tellOp,
    TITLE,
    XP,
    type Operation,
} from './operations';
import { PROPS, WRECKED, type PropId } from './camp';
import { near, POT, STILL_LEAVE } from './places';

const BATCH_MS = 45_000;
const BAND_LOW = 0.45;
const BAND_HIGH = 0.8;
const LOW_SPEED = 0.4;
const RISE_PER_S = 0.055;
const SURGE = 0.16;
const SURGE_MIN_MS = 7000;
const SURGE_MAX_MS = 12_000;
const VENT = 0.22;
const VENT_GAP_MS = 350;
const START_PRESSURE = 0.4;
const WRECK_MS = 120_000;
const BANKED_MS = 3 * 60_000;
const BLAST_RADIUS = 7;
const BLAST_DAMAGE = 60;
const BLAST_FIRE_MS = 30_000;

export const REPAIR_PRICE = 80;

/* Sounds (stock sounds directory) */

const FIRE_LOOP = '08c_ohen.wav';
const WHISTLE = '14b_parnipiskani.wav';
const VENT_SOUND = '15g_lokoplnykotel.wav';
const BLAST = '16b_vybuch.wav';

export interface Batch {
    opKey: string | null;
    /** Who tends it; null while the fire is banked. */
    operatorId: number | null;
    /** 0-1. */
    progress: number;
    /** 0-1; the pot blows at 1. */
    pressure: number;
    goodMs: number;
    totalMs: number;
    nextSurgeAt: number;
    lastVentAt: number;
    bankedAt: number;
}

const still = {
    batch: null as Batch | null,
    wreckedUntil: 0,
    sound: null as Sound | null,
    fire: null as Fire | null,
    blast: null as Fire | null,
    blastUntil: 0,
    lastWhistleAt: 0,
};
const props = new PlacedObjects<PropId>('Distillery prop', MAIN_WORLD);

export function stillBatch(): Batch | null {
    return still.batch;
}

/** The batch the player tends. */
export function batchOf(player: Player): Batch | null {
    const batch = still.batch;

    return batch && batch.operatorId === player.id ? batch : null;
}

export function wrecked(now = Date.now()): boolean {
    return still.wreckedUntil > now;
}

export function syncProps(): void {
    const broken = wrecked();

    props.sync(
        PROPS.map((spec): [PropId, ObjectSpec | null] => {
            // A piece the blast knocked over (or took, null) until the still stands again.
            const knocked = broken ? WRECKED[spec.id] : undefined;

            return [spec.id, knocked === undefined ? spec : knocked];
        }),
    );
}

export function clearProps(): void {
    props.clear();
}

export function forgetProps(): void {
    props.forget();
}

function stopEffects(): void {
    still.sound?.destroy();
    still.sound = null;
    still.fire?.destroy();
    still.fire = null;
}

// The blast's flames, put out a little after they die down.
export function clearBlast(): void {
    still.blast?.destroy();
    still.blast = null;
    still.blastUntil = 0;
}

function startEffects(): void {
    if (!still.sound && typeof Sound !== 'undefined') {
        still.sound = Sound.create(FIRE_LOOP, POT, { radius: 25, volume: 0.6 });
    }

    if (!still.fire && typeof Fire !== 'undefined') {
        still.fire = Fire.create(
            { x: POT.x, y: POT.y, z: POT.z },
            { radius: 0.6, height: 0.2, intensity: 0.5, damage: 5, virtualWorld: MAIN_WORLD },
        );
    }
}

function nextSurge(now: number): number {
    return now + SURGE_MIN_MS + Math.random() * (SURGE_MAX_MS - SURGE_MIN_MS);
}

/** G at the still: carries on a banked batch of the player's operation, or starts one. */
export function fireUp(player: Player, op: Operation): void {
    const opKey = opKeyOf(player);
    const now = Date.now();

    if (still.batch && still.batch.opKey === opKey && still.batch.operatorId === null) {
        still.batch.operatorId = player.id;
        still.batch.pressure = START_PRESSURE;
        still.batch.nextSurgeAt = nextSurge(now);
        startEffects();
        toast(
            player,
            'You take over the still. Keep the gauge in the green: tap G to vent when it climbs.',
            { title: TITLE, tone: 'gold' },
        );

        return;
    }

    op.molasses--;
    changed();
    still.batch = {
        opKey,
        operatorId: player.id,
        progress: 0,
        pressure: START_PRESSURE,
        goodMs: 0,
        totalMs: 0,
        nextSurgeAt: nextSurge(now),
        lastVentAt: 0,
        bankedAt: 0,
    };

    startEffects();
    hankSays(
        player,
        "Fire's lit. Watch the gauge: tap G to let off steam when it climbs past the green. Too low and it crawls, too high and she blows.",
        LINE.nod,
        9,
    );
}

export function vent(player: Player): void {
    const batch = batchOf(player);
    const now = Date.now();

    if (!batch || now - batch.lastVentAt < VENT_GAP_MS) {
        return;
    }

    batch.lastVentAt = now;
    batch.pressure = Math.max(0.05, batch.pressure - VENT);

    if (typeof Sound !== 'undefined') {
        Sound.play(VENT_SOUND, POT, { radius: 30, volume: 0.5 });
    }
}

/** The operator is gone: the batch waits, the fire banked. */
export function bank(batch: Batch): void {
    batch.operatorId = null;
    batch.bankedAt = Date.now();
    stopEffects();
}

export function bankFire(batch: Batch): void {
    const operator = playerById(batch.operatorId);

    bank(batch);

    if (operator) {
        interactions.prompt(operator, null);
        toast(
            operator,
            'You left the still, so Hank banked the fire. Come back and press G at the still to carry on.',
            { title: TITLE, tone: 'bad', seconds: 6 },
        );
    }
}

function blowUp(batch: Batch): void {
    const operator = playerById(batch.operatorId);

    still.batch = null;
    still.wreckedUntil = Date.now() + WRECK_MS;
    stopEffects();
    addHeat(batch.opKey, HEAT_BLAST);

    if (typeof Sound !== 'undefined') {
        Sound.play(BLAST, POT, { radius: 200, volume: 1 });
    }

    clearBlast();

    if (typeof Fire !== 'undefined') {
        still.blast =
            Fire.create(
                { x: POT.x, y: POT.y, z: POT.z },
                {
                    radius: 2.5,
                    height: 2,
                    intensity: 1,
                    damage: 30,
                    lifetime: 25_000,
                    virtualWorld: MAIN_WORLD,
                },
            ) ?? null;

        still.blastUntil = Date.now() + BLAST_FIRE_MS;
    }

    for (const player of World.getPlayers()) {
        if (!onStreet(player)) {
            continue;
        }

        const gap = distance(player.position, POT);

        if (gap > 25) {
            continue;
        }

        emit(player, 'fr:rum:flash', { strength: Math.max(0.2, 1 - gap / 25) });

        if (gap <= BLAST_RADIUS) {
            player.setHealth(
                Math.max(1, player.health - Math.round(BLAST_DAMAGE * (1 - gap / BLAST_RADIUS))),
            );
        }
    }

    hank.say(LINE.panic);

    if (operator) {
        interactions.prompt(operator, null);
        toast(
            operator,
            `The still blew! The batch is gone and the whole valley heard it. Hank can patch it in ${formatMinutes(WRECK_MS)}, ` +
                `or hold G at it and pay ${formatMoney(REPAIR_PRICE)} for the parts now.`,
            { title: TITLE, tone: 'bad', seconds: 9 },
        );

        feed('rum', [name(operator, COLORS.red), text(' blew up a still in the woods')]);
    }

    syncProps();
}

function finishBatch(batch: Batch): void {
    const op = batch.opKey === null ? undefined : ops.get(batch.opKey);

    still.batch = null;
    stopEffects();
    const operator = playerById(batch.operatorId);

    if (operator) {
        interactions.prompt(operator, null);
    }

    if (!op) {
        return;
    }

    const grade = Math.max(10, Math.round((batch.goodMs / Math.max(1, batch.totalMs)) * 100));

    op.casks.push({ q: grade, left: CASK_AGE_MS });
    changed();
    addHeat(batch.opKey, HEAT_BATCH);

    if (operator) {
        profiles.addXp(operator, XP.batch, 'Distilled rum');
        const verdict =
            grade >= 80
                ? "That's Havana quality."
                : grade >= 50
                  ? 'Drinkable.'
                  : "Rough stuff, but it'll sell.";

        hankSays(
            operator,
            `Runs clear. ${verdict} Into the cask it goes: ${formatMinutes(CASK_AGE_MS)} and you can bottle it at the casks.`,
            LINE.nod,
        );
    }
}

/** The patch is paid: the still stands again. */
export function repair(): void {
    still.wreckedUntil = 0;
    syncProps();
}

/** A batch on the still goes back to its operation as the sack it was (the map or the resource goes). */
export function returnBatch(): void {
    if (still.batch) {
        const op = still.batch.opKey === null ? undefined : ops.get(still.batch.opKey);

        if (op) {
            op.molasses = Math.min(MAX_MOLASSES, op.molasses + 1);
        }

        still.batch = null;
    }

    stopEffects();
}

export function tickStill(dt: number, now: number): void {
    if (still.blast && now >= still.blastUntil) {
        clearBlast();
    }

    if (still.wreckedUntil && !wrecked(now)) {
        still.wreckedUntil = 0;
        syncProps();
    }

    const batch = still.batch;

    if (!batch) {
        return;
    }

    if (batch.operatorId === null) {
        // A banked batch nobody comes back to frees the still for others.
        if (now - batch.bankedAt < BANKED_MS) {
            return;
        }

        still.batch = null;
        const op = batch.opKey === null ? undefined : ops.get(batch.opKey);

        if (op) {
            op.molasses = Math.min(MAX_MOLASSES, op.molasses + 1);
        }

        changed();
        tellOp(
            batch.opKey,
            'Nobody came back to the banked still, so Hank tipped the mash back into the sack.',
        );

        return;
    }

    const operator = playerById(batch.operatorId);

    if (
        !operator ||
        !onStreet(operator) ||
        operator.getVehicle() ||
        !near(operator, POT, STILL_LEAVE)
    ) {
        bankFire(batch);

        return;
    }

    const seconds = dt / 1000;

    batch.pressure += (RISE_PER_S + (Math.random() - 0.4) * 0.03) * seconds;

    if (now >= batch.nextSurgeAt) {
        batch.pressure += SURGE;
        batch.nextSurgeAt = nextSurge(now);
    }

    if (batch.pressure >= 1) {
        blowUp(batch);

        return;
    }

    const good = batch.pressure >= BAND_LOW && batch.pressure <= BAND_HIGH;
    const high = batch.pressure > BAND_HIGH;

    batch.totalMs += dt;

    if (good) {
        batch.goodMs += dt;
    }

    batch.progress += (dt / BATCH_MS) * (batch.pressure < BAND_LOW ? LOW_SPEED : 1);

    if (high && now - still.lastWhistleAt > 2500 && typeof Sound !== 'undefined') {
        still.lastWhistleAt = now;
        Sound.play(WHISTLE, POT, { radius: 60, volume: 0.9 });
    }

    const percent = Math.round(batch.pressure * 100);
    const state = high ? 'TOO HIGH! TAP G' : good ? 'GOOD' : 'LOW · LET IT BUILD';

    interactions.prompt(
        operator,
        `PRESSURE ${percent}% · ${state} · KEEP IT 45-80%`,
        Math.min(1, batch.pressure),
    );

    if (batch.progress >= 1) {
        finishBatch(batch);
    }
}
