import * as stylex from '@stylexjs/stylex';
import { type Toast, toasts } from '../store';
import { colors, fonts, motion } from '../tokens.stylex';

const slide = stylex.keyframes({
    from: { opacity: 0, transform: 'translateY(-0.6rem)', filter: 'blur(3px)' },
    to: { opacity: 1, transform: 'translateY(0)', filter: 'blur(0)' },
});

const styles = stylex.create({
    stack: {
        position: 'absolute',
        top: 'max(4vh, 1rem)',
        right: 'max(2.5vw, 1rem)',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-end',
        gap: '0.6rem',
        maxWidth: 'min(24rem, 60vw)',
        pointerEvents: 'none',
        zIndex: 50,
    },
    toast: {
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: '0.2rem',
        minWidth: '12rem',
        padding: '0.65rem 1.4rem 0.75rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        outlineWidth: '1px',
        outlineStyle: 'solid',
        outlineColor: colors.goldHair,
        outlineOffset: '-0.3rem',
        backgroundColor: colors.night,
        backgroundImage: 'radial-gradient(120% 120% at 50% 0%, rgba(201, 164, 92, 0.14), rgba(0, 0, 0, 0) 65%)',
        boxShadow: '0 0.6rem 1.8rem rgba(0, 0, 0, 0.65)',
        textAlign: 'center',
        animationName: slide,
        animationDuration: motion.medium,
        animationTimingFunction: motion.ease,
    },
    success: {
        borderColor: 'rgba(143, 181, 114, 0.6)',
    },
    error: {
        borderColor: 'rgba(184, 40, 31, 0.8)',
        backgroundImage: 'radial-gradient(120% 120% at 50% 0%, rgba(139, 26, 26, 0.35), rgba(0, 0, 0, 0) 65%)',
    },
    kicker: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        fontFamily: fonts.heading,
        fontSize: '0.62rem',
        fontWeight: 700,
        letterSpacing: '0.34em',
        textTransform: 'uppercase',
        color: colors.gold,
    },
    kickerSuccess: { color: colors.success },
    kickerError: { color: '#e88a7f' },
    rule: {
        width: '1.2rem',
        height: '1px',
        backgroundColor: 'currentColor',
        opacity: 0.6,
    },
    text: {
        fontFamily: fonts.heading,
        fontStyle: 'italic',
        fontSize: '1.05rem',
        lineHeight: 1.3,
        color: colors.cream,
        overflowWrap: 'anywhere',
        textShadow: '0 1px 1px rgba(0, 0, 0, 0.8)',
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
                    <span {...stylex.props(styles.kicker, toast.kind === 'success' && styles.kickerSuccess, toast.kind === 'error' && styles.kickerError)}>
                        <span {...stylex.props(styles.rule)} />
                        {kicker[toast.kind]}
                        <span {...stylex.props(styles.rule)} />
                    </span>
                    <span {...stylex.props(styles.text)}>{toast.text}</span>
                </div>
            ))}
        </div>
    );
}
