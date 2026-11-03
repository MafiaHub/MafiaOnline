import * as stylex from '@stylexjs/stylex';
import { type Toast, toasts } from '../store';
import { colors, motion, radii } from '../tokens.stylex';

const slide = stylex.keyframes({
    from: { opacity: 0, transform: 'translateY(-0.25rem)' },
    to: { opacity: 1, transform: 'translateY(0)' },
});

const styles = stylex.create({
    stack: {
        position: 'absolute',
        top: 'max(4vh, 1rem)',
        right: 'max(2.5vw, 1rem)',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-end',
        gap: '0.5rem',
        maxWidth: 'min(24rem, 60vw)',
        pointerEvents: 'none',
        zIndex: 50,
    },
    toast: {
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        gap: '0.25rem',
        minWidth: '12rem',
        padding: '0.75rem 1rem',
        borderWidth: '1px',
        borderLeftWidth: '3px',
        borderStyle: 'solid',
        borderColor: colors.line,
        borderLeftColor: colors.text,
        borderRadius: radii.sm,
        backgroundColor: colors.panel,
        animationName: slide,
        animationDuration: motion.medium,
        animationTimingFunction: motion.ease,
    },
    success: {
        borderLeftColor: colors.success,
    },
    error: {
        borderLeftColor: colors.accent,
    },
    kicker: {
        fontSize: '0.65rem',
        fontWeight: 600,
        letterSpacing: '0.18em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    kickerSuccess: { color: colors.success },
    kickerError: { color: colors.danger },
    text: {
        fontSize: '0.92rem',
        lineHeight: 1.4,
        color: colors.text,
        overflowWrap: 'anywhere',
    },
});

const kicker: Record<Toast['kind'], string> = {
    info: 'Word on the street',
    success: 'The line is open',
    error: 'Trouble',
};

export function Toasts() {
    return (
        <div {...stylex.props(styles.stack)} aria-live="polite">
            {toasts.value.map((toast) => (
                <div key={toast.id} {...stylex.props(styles.toast, toast.kind === 'success' && styles.success, toast.kind === 'error' && styles.error)}>
                    <span {...stylex.props(styles.kicker, toast.kind === 'success' && styles.kickerSuccess, toast.kind === 'error' && styles.kickerError)}>{kicker[toast.kind]}</span>
                    <span {...stylex.props(styles.text)}>{toast.text}</span>
                </div>
            ))}
        </div>
    );
}
