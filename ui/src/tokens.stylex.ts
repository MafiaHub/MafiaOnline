import * as stylex from '@stylexjs/stylex';

export const colors = stylex.defineVars({
    ink: '#0b0806',
    night: 'rgba(10, 7, 5, 0.94)',
    smoke: 'rgba(14, 10, 7, 0.78)',
    veil: 'rgba(12, 8, 6, 0.55)',
    panel: 'rgba(24, 17, 12, 0.92)',
    panelRaised: 'rgba(38, 27, 19, 0.95)',
    sepia: '#3a2819',
    leather: '#5b3a22',
    cream: '#f2e6cb',
    parchment: '#e3cfa6',
    muted: '#a8926d',
    faint: 'rgba(242, 230, 203, 0.45)',
    gold: '#c9a45c',
    goldBright: '#ecd08d',
    goldLine: 'rgba(201, 164, 92, 0.55)',
    goldHair: 'rgba(201, 164, 92, 0.22)',
    blood: '#8b1a1a',
    bloodBright: '#b8281f',
    bloodDeep: '#4d0d0f',
    success: '#8fb572',
    danger: '#e0625a',
});

export const fonts = stylex.defineVars({
    display: '"Limelight", "Playfair Display", Georgia, serif',
    heading: '"Playfair Display", Georgia, serif',
    body: '"EB Garamond", Georgia, serif',
});

export const motion = stylex.defineVars({
    quick: '140ms',
    medium: '260ms',
    slow: '600ms',
    ease: 'cubic-bezier(0.2, 0.7, 0.2, 1)',
});
