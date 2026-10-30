import * as stylex from '@stylexjs/stylex';
import type { ComponentChildren, JSX, Ref } from 'preact';
import { colors, fonts, motion, radii } from '../tokens.stylex';

const rise = stylex.keyframes({
    from: { opacity: 0, transform: 'translateY(0.25rem)' },
    to: { opacity: 1, transform: 'translateY(0)' },
});

// The one selection/focus marker used everywhere: a red bar on the left.
const marker = `inset 3px 0 0 ${colors.accent}`;

const styles = stylex.create({
    panel: {
        position: 'relative',
        backgroundColor: colors.panel,
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.line,
        borderRadius: radii.md,
        color: colors.text,
        padding: { default: '1.5rem', '@media (max-height: 620px)': '1rem 1.25rem' },
        animationName: rise,
        animationDuration: motion.medium,
        animationTimingFunction: motion.ease,
        animationFillMode: 'both',
    },
    heading: {
        margin: 0,
        fontFamily: fonts.display,
        fontWeight: 700,
        fontSize: '1.5rem',
        lineHeight: 1.2,
        color: colors.text,
    },
    // A double hairline under titles.
    divider: {
        height: 0,
        margin: { default: '0.75rem 0 1rem', '@media (max-height: 620px)': '0.5rem 0 0.75rem' },
        borderTopWidth: '3px',
        borderTopStyle: 'double',
        borderTopColor: colors.line,
    },
    button: {
        appearance: 'none',
        position: 'relative',
        display: 'inline-flex',
        alignItems: 'center',
        justifyContent: 'center',
        gap: '0.5rem',
        minHeight: '2.25rem',
        padding: '0 1rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.borderStrong,
        borderRadius: radii.sm,
        backgroundColor: 'transparent',
        color: colors.text,
        fontSize: '0.75rem',
        fontWeight: 600,
        letterSpacing: '0.14em',
        textTransform: 'uppercase',
        whiteSpace: 'nowrap',
        transitionProperty: 'transform',
        transitionDuration: motion.quick,
        transitionTimingFunction: motion.ease,
        outline: 'none',
        ':hover': {
            borderColor: colors.text,
            backgroundColor: colors.raised,
        },
        ':focus-visible': {
            borderColor: colors.text,
            boxShadow: marker,
        },
        ':active': {
            transform: 'translateY(1px)',
        },
        ':disabled': {
            opacity: 0.4,
            borderColor: colors.border,
            backgroundColor: 'transparent',
        },
    },
    primary: {
        backgroundColor: colors.accent,
        borderColor: colors.accent,
        color: colors.text,
        ':hover': {
            backgroundColor: colors.accentHover,
            borderColor: colors.accentHover,
        },
        ':focus-visible': {
            borderColor: colors.text,
            boxShadow: `inset 0 0 0 1px ${colors.text}`,
        },
    },
    large: {
        minHeight: '2.75rem',
        fontSize: '0.85rem',
        letterSpacing: '0.2em',
        padding: '0 1.5rem',
    },
    wide: {
        width: '100%',
    },
    ghost: {
        borderColor: 'transparent',
        backgroundColor: 'transparent',
        ':hover': {
            borderColor: 'transparent',
            backgroundColor: colors.raised,
        },
    },
    field: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.375rem',
        minWidth: 0,
    },
    label: {
        display: 'flex',
        justifyContent: 'space-between',
        fontSize: '0.68rem',
        fontWeight: 600,
        letterSpacing: '0.14em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    hint: {
        fontWeight: 400,
        letterSpacing: 0,
        textTransform: 'none',
        color: colors.faint,
    },
    input: {
        appearance: 'none',
        width: '100%',
        minWidth: 0,
        height: { default: '2.25rem', '@media (max-height: 620px)': '2rem' },
        padding: '0 0.75rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.border,
        borderRadius: radii.sm,
        backgroundColor: colors.bg,
        color: colors.text,
        fontSize: '0.95rem',
        outline: 'none',
        caretColor: colors.accentHover,
        '::placeholder': {
            color: colors.faint,
        },
        ':focus': {
            borderColor: colors.line,
            boxShadow: marker,
        },
    },
    invalid: {
        borderColor: colors.danger,
    },
    toggle: {
        appearance: 'none',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: '1rem',
        width: '100%',
        padding: '0.5rem 0',
        borderWidth: '0 0 1px 0',
        borderStyle: 'solid',
        borderColor: colors.border,
        backgroundColor: 'transparent',
        color: colors.text,
        fontSize: '0.92rem',
        textAlign: 'left',
        outline: 'none',
        ':focus-visible': {
            paddingLeft: '0.6rem',
            boxShadow: marker,
        },
    },
    switchTrack: {
        position: 'relative',
        flexShrink: 0,
        width: '2.25rem',
        height: '1.25rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.borderStrong,
        borderRadius: radii.sm,
        backgroundColor: colors.bg,
    },
    switchOn: {
        borderColor: colors.accent,
        backgroundColor: colors.accent,
    },
    switchKnob: {
        position: 'absolute',
        top: '50%',
        left: '0.1875rem',
        width: '0.75rem',
        height: '0.75rem',
        marginTop: '-0.375rem',
        backgroundColor: colors.muted,
        transitionProperty: 'transform',
        transitionDuration: motion.quick,
        transitionTimingFunction: motion.ease,
    },
    switchKnobOn: {
        transform: 'translateX(1rem)',
        backgroundColor: colors.text,
    },
    stepper: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        color: colors.text,
    },
    stepButton: {
        minHeight: '1.75rem',
        minWidth: '1.75rem',
        padding: 0,
        letterSpacing: 0,
    },
    stepValue: {
        minWidth: '4rem',
        textAlign: 'center',
        fontVariantNumeric: 'tabular-nums',
    },
});

export function Panel(props: { children: ComponentChildren; style?: stylex.StyleXStyles; delay?: number }) {
    return (
        <section {...stylex.props(styles.panel, props.style)} style={props.delay ? { animationDelay: `${props.delay}ms` } : undefined}>
            {props.children}
        </section>
    );
}

export function Heading(props: { children: ComponentChildren }) {
    return (
        <>
            <h2 {...stylex.props(styles.heading)}>{props.children}</h2>
            <Divider />
        </>
    );
}

export function Divider() {
    return <div {...stylex.props(styles.divider)} aria-hidden="true" />;
}

export function Button(
    props: {
        children: ComponentChildren;
        variant?: 'primary' | 'ghost';
        size?: 'large';
        wide?: boolean;
        style?: stylex.StyleXStyles;
    } & Omit<JSX.ButtonHTMLAttributes<HTMLButtonElement>, 'style' | 'size'>,
) {
    const { children, variant, size, wide, style, ...rest } = props;
    return (
        <button
            type="button"
            {...rest}
            {...stylex.props(styles.button, variant === 'primary' && styles.primary, variant === 'ghost' && styles.ghost, size === 'large' && styles.large, wide && styles.wide, style)}
        >
            {children}
        </button>
    );
}

export function Field(props: {
    label: string;
    hint?: string;
    value: string;
    onInput: (value: string) => void;
    onEnter?: () => void;
    type?: 'text' | 'password';
    placeholder?: string;
    invalid?: boolean;
    inputRef?: Ref<HTMLInputElement>;
    maxLength?: number;
    inputMode?: 'numeric' | 'text';
    style?: stylex.StyleXStyles;
}) {
    return (
        <label {...stylex.props(styles.field, props.style)}>
            <span {...stylex.props(styles.label)}>
                {props.label}
                {props.hint && <span {...stylex.props(styles.hint)}>{props.hint}</span>}
            </span>
            <input
                ref={props.inputRef}
                {...stylex.props(styles.input, props.invalid && styles.invalid)}
                type={props.type ?? 'text'}
                value={props.value}
                placeholder={props.placeholder}
                maxLength={props.maxLength}
                inputMode={props.inputMode}
                spellcheck={false}
                autocomplete="off"
                onInput={(event) => props.onInput((event.currentTarget as HTMLInputElement).value)}
                onKeyDown={(event) => {
                    if (event.key === 'Enter' && props.onEnter) {
                        event.preventDefault();
                        props.onEnter();
                    }
                }}
            />
        </label>
    );
}

export function Toggle(props: { label: string; value: boolean; onChange: (value: boolean) => void }) {
    return (
        <button type="button" {...stylex.props(styles.toggle)} onClick={() => props.onChange(!props.value)}>
            <span>{props.label}</span>
            <span {...stylex.props(styles.switchTrack, props.value && styles.switchOn)}>
                <span {...stylex.props(styles.switchKnob, props.value && styles.switchKnobOn)} />
            </span>
        </button>
    );
}

export function Stepper(props: { label: string; value: number; format: (value: number) => string; step: number; min: number; max: number; onChange: (value: number) => void }) {
    const change = (delta: number) => {
        const next = Math.round((props.value + delta) * 100) / 100;
        props.onChange(Math.min(props.max, Math.max(props.min, next)));
    };
    return (
        <div {...stylex.props(styles.toggle)}>
            <span>{props.label}</span>
            <span {...stylex.props(styles.stepper)}>
                <Button style={styles.stepButton} onClick={() => change(-props.step)} disabled={props.value <= props.min}>
                    −
                </Button>
                <span {...stylex.props(styles.stepValue)}>{props.format(props.value)}</span>
                <Button style={styles.stepButton} onClick={() => change(props.step)} disabled={props.value >= props.max}>
                    +
                </Button>
            </span>
        </div>
    );
}
