// Draw takes 0xAARRGGBB numbers and Hud.showMessage 0xRRGGBB; the server
// names its colours as '#rrggbb' strings.

const PARCHMENT = 0xf3e9d6;

/** The 0xRRGGBB of a '#rrggbb' colour; parchment for anything else. */
export function rgb(hex: unknown): number {
    return typeof hex === 'string' && /^#[0-9a-f]{6}$/i.test(hex)
        ? parseInt(hex.slice(1), 16)
        : PARCHMENT;
}

/** A 0xRRGGBB colour at an opacity from 0 to 1, as Draw takes it. */
export function withAlpha(color: number, alpha = 1): number {
    return Math.round(alpha * 255) * 0x1000000 + color;
}

/** A '#rrggbb' colour at an opacity from 0 to 1, as Draw takes it. */
export function argb(hex: unknown, alpha = 1): number {
    return withAlpha(rgb(hex), alpha);
}

/** The Free Ride HUD palette, shared by the counter cards and key prompts. */
export const COLORS = {
    /** Labels and key prompts. */
    gold: 0xffe7c47d,
    text: 0xfff5e6cb,
    muted: 0xffb6aa91,
    /** The rules across a counter card. */
    rule: 0xff9c7743,
    panel: 0xe6121519,
    /** Hud.showMessage takes no alpha. */
    message: 0xe8c57d,
} as const;
