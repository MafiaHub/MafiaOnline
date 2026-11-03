// Orders beyond the usual buyers: a wedding in the village sometimes wants
// rum (/rum wedding takes the order), and now and then it is party night at
// Salieri's, when Luigi pays half again.
import * as profiles from '../lib/profiles';
import { feed, name, text, toast, COLORS } from '../lib/notify';
import { formatMinutes, formatMoney } from '../lib/util';
import { unload, type Load } from './cargo';
import {
    changed,
    members,
    opKeyOf,
    QUEST,
    RUM,
    stageOf,
    takeCases,
    tellOp,
    TITLE,
    XP,
    type Operation,
} from './operations';

const WEDDING_CHANCE = 0.3;
const WEDDING_CASES = 4;
const WEDDING_PAY = 260;
const WEDDING_OFFER_MS = 2 * 60_000;
const WEDDING_MS = 8 * 60_000;
const PARTY_EVERY_MS = 15 * 60_000;
const PARTY_MS = 5 * 60_000;

export const PARTY_PRICE = 1.5;
const OP_EVENT_GAP_MS = 12 * 60_000;

export interface Wedding {
    state: 'offered' | 'accepted';
    until: number;
}

// opKey -> the Kowalskis' order
const weddings = new Map<string, Wedding>();
let party = { until: 0, nextAt: 0 };

export function weddingOf(opKey: string | null): Wedding | null {
    return opKey === null ? null : (weddings.get(opKey) ?? null);
}

/** Whether the op's crew took the order and the load is theirs to bring. */
export function weddingAccepted(opKey: string | null): boolean {
    return weddingOf(opKey)?.state === 'accepted';
}

export function clearWeddings(): void {
    weddings.clear();
}

export function partyOn(now = Date.now()): boolean {
    return party.until > now;
}

export function partyUntil(): number {
    return party.until;
}

export function scheduleParty(now: number): void {
    party.nextAt = now + PARTY_EVERY_MS;
}

/** A roll for the op's wedding order, with its cooldown (cooldowns.weddingAt). */
export function rollWedding(
    opKey: string,
    op: Operation,
    wait: { weddingAt: number },
    now: number,
): void {
    if (
        weddings.has(opKey) ||
        op.cases.n < WEDDING_CASES ||
        !members(opKey).length ||
        now - wait.weddingAt <= OP_EVENT_GAP_MS ||
        Math.random() >= WEDDING_CHANCE
    ) {
        return;
    }

    wait.weddingAt = now;
    weddings.set(opKey, { state: 'offered', until: now + WEDDING_OFFER_MS });
    tellOp(
        opKey,
        `The Kowalskis are marrying off their daughter in the village and want ${WEDDING_CASES} cases of rum, ${formatMoney(WEDDING_PAY)}. ` +
            'Type /rum wedding (or F2 → Jobs) to take the order.',
        'gold',
        10,
    );
}

export function expireWeddings(now: number): void {
    for (const [opKey, offer] of [...weddings]) {
        if (now < offer.until) {
            continue;
        }

        weddings.delete(opKey);
        tellOp(
            opKey,
            offer.state === 'accepted'
                ? 'The wedding went dry: the Kowalskis found someone else.'
                : "The Kowalskis' order went to someone else.",
            'bad',
        );
    }
}

export function rollParty(now: number): void {
    if (now < party.nextAt) {
        return;
    }

    party = { until: now + PARTY_MS, nextAt: now + PARTY_EVERY_MS * (0.8 + Math.random() * 0.4) };

    for (const player of World.getPlayers()) {
        if (stageOf(player) >= QUEST.FIRST_SALE) {
            toast(
                player,
                `Party night at Salieri's: Luigi pays half again for rum for the next ${formatMinutes(PARTY_MS)}.`,
                { title: TITLE, tone: 'gold', seconds: 7 },
            );
        }
    }
}

/** /rum wedding: takes the order offered to the player's operation. */
export function acceptWedding(player: Player): void {
    const opKey = opKeyOf(player);
    const offer = weddingOf(opKey);

    if (!offer) {
        toast(player, 'There is no wedding order for you right now.', { title: TITLE });

        return;
    }

    if (offer.state === 'accepted') {
        toast(player, 'Your crew already took the order: bring 4 cases to the village crossing.', {
            title: TITLE,
        });

        return;
    }

    offer.state = 'accepted';
    offer.until = Date.now() + WEDDING_MS;
    tellOp(
        opKey,
        `${player.nickname} took the wedding order: ${WEDDING_CASES} cases to the village crossing within ${formatMinutes(WEDDING_MS)}, ${formatMoney(WEDDING_PAY)}.`,
        'gold',
        8,
    );
}

/** A car of the op's rum stopped at the village: true when the wedding took its cases. */
export function weddingDelivery(driver: Player, vehicle: Vehicle, entry: Load): boolean {
    if (
        !weddingAccepted(entry.opKey) ||
        entry.n < WEDDING_CASES ||
        opKeyOf(driver) !== entry.opKey ||
        entry.opKey === null
    ) {
        return false;
    }

    weddings.delete(entry.opKey);
    takeCases(entry, WEDDING_CASES);

    if (entry.n <= 0) {
        unload(vehicle);
    }

    driver.giveMoney(WEDDING_PAY);
    profiles.addXp(driver, XP.wedding, 'Rum for a wedding');
    changed();
    toast(
        driver,
        `The Kowalskis take ${WEDDING_CASES} cases and pay ${formatMoney(WEDDING_PAY)}. The band strikes up.`,
        { title: TITLE, tone: 'good', seconds: 7 },
    );

    feed('rum', [
        name(driver, RUM),
        text(' brought the rum to a village wedding · '),
        text(formatMoney(WEDDING_PAY), COLORS.gold),
    ]);

    return true;
}
