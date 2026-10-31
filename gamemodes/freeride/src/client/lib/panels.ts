// What the counters and key prompts show: the card at a shop or bar counter
// (drawn by the HUD page, hud.html) and the one-line key prompts drawn
// straight onto the screen (the police call box, the job key).
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

/** One thing on a counter's list. */
export interface CounterLine {
    name: string;
    /** "$25", "A ROUND", "NO CHARGE". */
    price: string;
    note: string;
    /** The player has the cash for it (a free one always). */
    affordable: boolean;
}

/** The counter the player stands at, as the HUD page draws it (hud.html #counter). */
export interface CounterCard {
    title: string;
    tagline: string;
    lines: CounterLine[];
    /** The selected line, from 0. */
    index: number;
    /** What B does: ORDER, BUY, ACCEPT. */
    action: string;
    money: number;
    /** The keys as the player bound them: "[N]", "[B]". */
    nextKey: string;
    buyKey: string;
}

// Each module that has counters (shops, bars) says every frame which card it
// has, or none; the HUD shows the first, and is told only when it changes.
const counters = new Map<string, CounterCard | null>();
let sink: ((card: CounterCard | null) => void) | null = null;
let shown = '';

/** Where the card goes: the HUD page (hud.ts). */
export function setCounterSink(next: (card: CounterCard | null) => void): void {
    sink = next;
    shown = '';
}

/** The card of `source`'s counter the player stands at, or null away from them. */
export function showCounter(
    source: string,
    card: Omit<CounterCard, 'nextKey' | 'buyKey'> | null,
): void {
    counters.set(source, card && { ...card, nextKey: keyHint(NEXT_KEY), buyKey: keyHint(BUY_KEY) });
    const current = [...counters.values()].find((entry) => entry !== null) ?? null;
    const key = current ? JSON.stringify(current) : '';

    if (key !== shown) {
        shown = key;
        sink?.(current);
    }
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
