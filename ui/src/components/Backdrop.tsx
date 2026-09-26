import * as stylex from '@stylexjs/stylex';
import { preferences } from '../store';

// Tints the game's own menu scene rather than hiding it: a sepia wash, a hard
// left-hand shadow for legibility, a vignette and optional film grain.
const grain = stylex.keyframes({
    '0%': { transform: 'translate(0, 0)' },
    '20%': { transform: 'translate(-3%, 2%)' },
    '40%': { transform: 'translate(2%, -3%)' },
    '60%': { transform: 'translate(-2%, -1%)' },
    '80%': { transform: 'translate(3%, 3%)' },
    '100%': { transform: 'translate(0, 0)' },
});

const flicker = stylex.keyframes({
    '0%': { opacity: 0.92 },
    '50%': { opacity: 1 },
    '100%': { opacity: 0.94 },
});

// feTurbulence noise as a tiny inline SVG: no image asset, no network.
const noise =
    "url(\"data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='160' height='160'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.9' numOctaves='2' stitchTiles='stitch'/><feColorMatrix values='0 0 0 0 0.9 0 0 0 0 0.8 0 0 0 0 0.6 0 0 0 0.55 0'/></filter><rect width='100%' height='100%' filter='url(%23n)'/></svg>\")";

const styles = stylex.create({
    root: {
        position: 'absolute',
        inset: 0,
        overflow: 'hidden',
        pointerEvents: 'none',
    },
    wash: {
        position: 'absolute',
        inset: 0,
        backgroundImage:
            'linear-gradient(90deg, rgba(8, 5, 3, 0.94) 0%, rgba(12, 8, 5, 0.82) 34%, rgba(20, 12, 7, 0.42) 62%, rgba(20, 12, 7, 0.2) 100%), linear-gradient(180deg, rgba(58, 36, 18, 0.35), rgba(12, 8, 5, 0.55))',
        animationName: flicker,
        animationDuration: '5s',
        animationDirection: 'alternate',
        animationIterationCount: 'infinite',
        animationTimingFunction: 'ease-in-out',
    },
    vignette: {
        position: 'absolute',
        inset: 0,
        backgroundImage: 'radial-gradient(ellipse at 60% 45%, rgba(0, 0, 0, 0) 45%, rgba(0, 0, 0, 0.72) 100%)',
    },
    grain: {
        position: 'absolute',
        inset: '-10%',
        backgroundImage: noise,
        backgroundSize: '160px 160px',
        opacity: 0.16,
        mixBlendMode: 'overlay',
        animationName: grain,
        animationDuration: '0.9s',
        animationTimingFunction: 'steps(6)',
        animationIterationCount: 'infinite',
    },
    bars: {
        position: 'absolute',
        left: 0,
        right: 0,
        height: '3.2vh',
        backgroundColor: 'rgba(0, 0, 0, 0.85)',
    },
    top: { top: 0 },
    bottom: { bottom: 0 },
});

export function Backdrop() {
    const prefs = preferences.value;
    return (
        <div {...stylex.props(styles.root)} aria-hidden="true">
            <div {...stylex.props(styles.wash)} />
            <div {...stylex.props(styles.vignette)} />
            {prefs.filmGrain && !prefs.reduceMotion && <div {...stylex.props(styles.grain)} />}
            <div {...stylex.props(styles.bars, styles.top)} />
            <div {...stylex.props(styles.bars, styles.bottom)} />
        </div>
    );
}
