import * as stylex from '@stylexjs/stylex';

// Tints the game's own menu scene rather than hiding it: hard noir lighting.
// The left, where the panels sit, is in deep shadow; a diagonal edge lets the
// scene through on the right, crossed by faint venetian-blind shadows.
// Static on purpose; CEF repaints every frame of an animation.
const styles = stylex.create({
    root: {
        position: 'absolute',
        inset: 0,
        overflow: 'hidden',
        pointerEvents: 'none',
        backgroundImage:
            'linear-gradient(104deg, rgba(8, 8, 9, 0.95) 0%, rgba(8, 8, 9, 0.9) 44%, rgba(8, 8, 9, 0.55) 52%, rgba(8, 8, 9, 0.4) 100%), repeating-linear-gradient(166deg, rgba(0, 0, 0, 0.2) 0 1.5rem, rgba(0, 0, 0, 0) 1.5rem 2.6rem)',
    },
});

export function Backdrop() {
    return <div {...stylex.props(styles.root)} aria-hidden="true" />;
}
