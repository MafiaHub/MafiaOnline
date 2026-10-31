// The panels drawn straight onto the screen: the card at a shop or bar counter
// and the one-line key prompts (the police call box, the job key).
import { COLORS } from './color';

/** "[N]": the key the player now presses for `key`, as prompts show it. */
export function keyHint(key: string): string {
    return `[${Settings.getKeyLabel(key).toUpperCase()}]`;
}

const NEXT_KEY = 'n';
const BUY_KEY = 'b';

/**
 * Shops and bars share N (the next offer) and B (buy it). Each binds its own
 * handler, which acts only at its own counter.
 */
export function bindCounterKeys(buy: () => void, next: () => void): void {
    Settings.describeKey(BUY_KEY, 'Buy or accept at a shop or bar counter');
    Settings.describeKey(NEXT_KEY, 'Next offer at a shop or bar counter');
    Key.bind(BUY_KEY, buy);
    Key.bind(NEXT_KEY, next);
}

export interface CounterCard {
    title: string;
    tagline: string;
    /** The selected offer and its price. */
    offer: string;
    note: string;
    /** The selected offer's place in the list, from 0. */
    index: number;
    count: number;
    /** What B does: ORDER, BUY, ACCEPT. */
    action: string;
    money: number;
}

/** The card of the counter the player stands at, at the bottom of the screen. */
export function drawCounterCard(card: CounterCard): void {
    const { width, height } = Web.getScreenSize();
    const cardWidth = Math.min(520, width - 32);
    const cardHeight = 184;
    const left = (width - cardWidth) / 2;
    const top = height - cardHeight - 28;
    const center = width / 2;

    Draw.rect(left, top, cardWidth, cardHeight, COLORS.panel);
    Draw.line(left + 12, top + 11, left + cardWidth - 12, top + 11, 2, COLORS.rule);
    Draw.text(card.title.toUpperCase(), center, top + 38, 22, COLORS.text, 3);
    Draw.text(card.tagline.toUpperCase(), center, top + 61, 12, COLORS.muted, 3);
    Draw.line(left + 28, top + 70, left + cardWidth - 28, top + 70, 1, COLORS.rule);
    Draw.text(card.offer, center, top + 102, 19, COLORS.text, 3);
    Draw.text(card.note, center, top + 126, 12, COLORS.muted, 3);
    Draw.line(left + 28, top + 138, left + cardWidth - 28, top + 138, 1, COLORS.rule);
    Draw.text(
        `${keyHint(NEXT_KEY)} NEXT  ${card.index + 1}/${card.count}      ${keyHint(BUY_KEY)} ${card.action}      CASH $${card.money}`,
        center,
        top + 164,
        13,
        COLORS.gold,
        3,
    );
}

const PROMPT_FILL = 0xd0121519;
const PROMPT_EDGE = 0x70e7c47d;

export interface PromptPlacement {
    /** From the plate's top to the bottom of the screen. */
    rise: number;
    width: number;
    height?: number;
    /** A gold edge down the left and along the top. */
    accent?: boolean;
}

/** A key prompt on a dark plate centred near the bottom of the screen; returns where the plate went. */
export function drawPrompt(
    text: string,
    { rise, width, height = 36, accent = false }: PromptPlacement,
): { center: number; top: number } {
    const screen = Web.getScreenSize();
    const center = screen.width / 2;
    const left = center - width / 2;
    const top = screen.height - rise;

    Draw.rect(left, top, width, height, PROMPT_FILL);

    if (accent) {
        Draw.rect(left, top, 4, height, COLORS.gold);
        Draw.rect(left + 4, top, width - 4, 1, PROMPT_EDGE);
    }

    Draw.text(text, accent ? center + 2 : center, top + 24, 15, COLORS.gold, 3);

    return { center, top };
}
