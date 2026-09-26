import * as stylex from '@stylexjs/stylex';
import type { ComponentChildren, JSX } from 'preact';
import { theme } from './tokens.stylex';

export function Arrow() {
    return (
        <svg
            width="18"
            height="18"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="1.5"
            aria-hidden="true"
        >
            <path d="M4 12h15m-5-5 5 5-5 5" />
        </svg>
    );
}

export function Seal() {
    return (
        <svg width="32" height="36" viewBox="0 0 32 36" fill="none" aria-hidden="true">
            <path d="m16 1 15 9v16l-15 9L1 26V10Z" stroke="currentColor" opacity=".5" />
            <path d="M10 11v14h6m4-14v14m-6-7h6" stroke="currentColor" stroke-width="1.5" />
        </svg>
    );
}

export function Panel({ children, title }: { children: ComponentChildren; title: string }) {
    return (
        <section
            {...stylex.props(styles.panel)}
            role="dialog"
            aria-modal="false"
            aria-labelledby={title}
        >
            {children}
        </section>
    );
}

export function Button({
    children,
    secondary = false,
    ...props
}: JSX.ButtonHTMLAttributes<HTMLButtonElement> & { secondary?: boolean }) {
    return (
        <button {...props} {...stylex.props(styles.button, secondary && styles.secondary)}>
            {children}
        </button>
    );
}

export function Field({
    label,
    hint,
    trailing,
    ...props
}: JSX.InputHTMLAttributes<HTMLInputElement> & {
    id: string;
    label: string;
    hint?: string;
    trailing?: ComponentChildren;
}) {
    return (
        <div {...stylex.props(styles.field)}>
            <label {...stylex.props(styles.label)} htmlFor={props.id}>
                {label}
            </label>
            <div {...stylex.props(styles.inputWrap)}>
                <input
                    {...props}
                    {...stylex.props(styles.input, Boolean(trailing) && styles.withTrailing)}
                    aria-describedby={hint ? `${props.id}-hint` : undefined}
                />
                {trailing && <div {...stylex.props(styles.trailing)}>{trailing}</div>}
            </div>
            {hint && (
                <p id={`${props.id}-hint`} {...stylex.props(styles.hint)}>
                    {hint}
                </p>
            )}
        </div>
    );
}

const styles = stylex.create({
    panel: {
        width: '100%',
        maxWidth: 440,
        padding: '30px 36px 26px',
        backgroundColor: theme.panel,
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        borderRadius: theme.radius,
        boxShadow: '0 24px 100px rgba(0,0,0,.45)',
        position: 'relative',
        '::before': {
            content: '""',
            position: 'absolute',
            top: -1,
            left: '36%',
            right: '36%',
            height: 1,
            backgroundColor: theme.brass,
        },
        '@media (max-width: 520px)': { padding: '24px 22px' },
        '@media (max-height: 820px)': { paddingTop: 24, paddingBottom: 20 },
    },
    button: {
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        gap: 14,
        width: '100%',
        minHeight: 48,
        padding: '12px 16px',
        fontSize: 12,
        fontWeight: 600,
        fontFamily: theme.body,
        letterSpacing: '.025em',
        color: theme.ink,
        backgroundColor: { default: theme.brass, ':hover': '#e0cda2' },
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: 'transparent',
        borderRadius: theme.radius,
        cursor: 'pointer',
        transition: 'background-color 160ms, opacity 160ms',
        ':disabled': { opacity: 0.5, cursor: 'wait' },
        ':focus-visible': {
            outlineWidth: 2,
            outlineStyle: 'solid',
            outlineColor: theme.paper,
            outlineOffset: 4,
        },
    },
    secondary: {
        width: 'auto',
        minHeight: 34,
        backgroundColor: { default: 'transparent', ':hover': 'rgba(255,255,255,.07)' },
        color: theme.muted,
        borderColor: theme.line,
        fontSize: 11,
        fontWeight: 400,
    },
    field: { display: 'flex', flexDirection: 'column', gap: 8 },
    label: {
        color: theme.paper,
        fontSize: 10,
        letterSpacing: '.1em',
        textTransform: 'uppercase',
        fontWeight: 500,
    },
    inputWrap: { position: 'relative' },
    input: {
        width: '100%',
        height: 46,
        backgroundColor: theme.field,
        color: theme.paper,
        padding: '0 13px',
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        borderRadius: theme.radius,
        fontFamily: theme.body,
        fontSize: 12,
        transition: 'border-color 150ms',
        outlineStyle: 'none',
        '::placeholder': { color: '#92958e' },
        ':focus': { borderColor: theme.brass },
        ':disabled': { opacity: 0.55 },
    },
    withTrailing: { paddingRight: 45 },
    trailing: {
        position: 'absolute',
        top: 0,
        right: 0,
        bottom: 0,
        display: 'flex',
        alignItems: 'center',
    },
    hint: { margin: 0, color: theme.muted, fontSize: 10, lineHeight: 1.6 },
});
