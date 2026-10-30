import * as stylex from '@stylexjs/stylex';

// Clean noir: black, paper white and the greys between them, one blood red
// for primary actions, focus and selection, and two status colors.
export const colors = stylex.defineVars({
    bg: '#0b0b0c',
    panel: '#111112',
    raised: '#1a1a1b',
    hover: '#242425',
    border: '#2c2c2e',
    borderStrong: '#4a4a4c',
    line: '#77746f',
    text: '#ece8e1',
    muted: '#9b9892',
    faint: '#64625e',
    accent: '#a3201c',
    accentHover: '#c42b25',
    accentSoft: 'rgba(163, 32, 28, 0.2)',
    scrim: 'rgba(6, 6, 7, 0.78)',
    success: '#7fb08a',
    danger: '#e0544d',
});

// A display serif for titles and headings only; Inter for everything else.
export const fonts = stylex.defineVars({
    display: '"Playfair Display", Georgia, serif',
    sans: '"Inter", "Segoe UI", Arial, sans-serif',
});

// Sharp corners: controls are square, panels barely softened.
export const radii = stylex.defineVars({
    sm: '0px',
    md: '2px',
});

// Short and only on interaction: CEF paints on the CPU, so anything that
// keeps moving costs frame time in the game.
export const motion = stylex.defineVars({
    quick: '120ms',
    medium: '180ms',
    ease: 'cubic-bezier(0.2, 0.7, 0.2, 1)',
});
