import * as stylex from '@stylexjs/stylex';
import type { ComponentChildren, JSX, Ref } from 'preact';
import { colors, fonts, motion } from '../tokens.stylex';

const rise = stylex.keyframes({
    from: { opacity: 0, transform: 'translateY(0.6rem)' },
    to: { opacity: 1, transform: 'translateY(0)' },
});

const styles = stylex.create({
    panel: {
        position: 'relative',
        backgroundColor: colors.panel,
        backgroundImage: 'linear-gradient(180deg, rgba(255, 236, 196, 0.045), rgba(0, 0, 0, 0) 38%), radial-gradient(120% 90% at 50% 0%, rgba(139, 26, 26, 0.12), rgba(0, 0, 0, 0) 60%)',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        outlineWidth: '1px',
        outlineStyle: 'solid',
        outlineColor: colors.goldHair,
        outlineOffset: '-0.4rem',
        boxShadow: '0 1.2rem 3rem rgba(0, 0, 0, 0.55), inset 0 0 2.5rem rgba(0, 0, 0, 0.45)',
        padding: { default: '1.6rem 1.8rem', '@media (max-height: 620px)': '1.1rem 1.3rem' },
        animationName: rise,
        animationDuration: motion.slow,
        animationTimingFunction: motion.ease,
        animationFillMode: 'both',
        '::before': {
            content: '""',
            position: 'absolute',
            top: '-0.35rem',
            left: '-0.35rem',
            width: '1.4rem',
            height: '1.4rem',
            borderTopWidth: '2px',
            borderLeftWidth: '2px',
            borderTopStyle: 'solid',
            borderLeftStyle: 'solid',
            borderColor: colors.gold,
            pointerEvents: 'none',
        },
        '::after': {
            content: '""',
            position: 'absolute',
            right: '-0.35rem',
            bottom: '-0.35rem',
            width: '1.4rem',
            height: '1.4rem',
            borderRightWidth: '2px',
            borderBottomWidth: '2px',
            borderRightStyle: 'solid',
            borderBottomStyle: 'solid',
            borderColor: colors.gold,
            pointerEvents: 'none',
        },
    },
    heading: {
        margin: 0,
        fontFamily: fonts.heading,
        fontWeight: 700,
        fontSize: '1.35rem',
        letterSpacing: '0.18em',
        textTransform: 'uppercase',
        color: colors.cream,
        textAlign: 'center',
    },
    divider: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.6rem',
        margin: { default: '0.7rem 0 1.1rem', '@media (max-height: 620px)': '0.45rem 0 0.7rem' },
        color: colors.gold,
        fontSize: '0.6rem',
    },
    dividerLine: {
        flexGrow: 1,
        height: '1px',
        backgroundImage: `linear-gradient(90deg, rgba(0, 0, 0, 0), ${colors.goldLine}, rgba(0, 0, 0, 0))`,
    },
    button: {
        appearance: 'none',
        position: 'relative',
        display: 'inline-flex',
        alignItems: 'center',
        justifyContent: 'center',
        gap: '0.5rem',
        minHeight: '2.4rem',
        padding: '0 1.3rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        backgroundColor: 'rgba(20, 14, 10, 0.6)',
        color: colors.parchment,
        fontFamily: fonts.heading,
        fontSize: '0.82rem',
        fontWeight: 700,
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
        whiteSpace: 'nowrap',
        transitionProperty: 'background-color, color, border-color, transform, box-shadow',
        transitionDuration: motion.quick,
        transitionTimingFunction: motion.ease,
        outline: 'none',
        ':hover': {
            borderColor: colors.gold,
            color: colors.goldBright,
            backgroundColor: 'rgba(58, 40, 25, 0.75)',
        },
        ':focus-visible': {
            borderColor: colors.goldBright,
            boxShadow: `0 0 0 1px ${colors.goldBright}, 0 0 1.2rem rgba(236, 208, 141, 0.25)`,
        },
        ':active': {
            transform: 'translateY(1px)',
        },
        ':disabled': {
            opacity: 0.4,
            borderColor: colors.goldHair,
            color: colors.muted,
            backgroundColor: 'rgba(20, 14, 10, 0.4)',
        },
    },
    primary: {
        backgroundColor: colors.blood,
        backgroundImage: 'linear-gradient(180deg, rgba(255, 255, 255, 0.12), rgba(0, 0, 0, 0.2))',
        borderColor: colors.gold,
        color: colors.cream,
        boxShadow: '0 0.4rem 1.2rem rgba(77, 13, 15, 0.55), inset 0 0 0 1px rgba(236, 208, 141, 0.25)',
        textShadow: '0 1px 0 rgba(0, 0, 0, 0.5)',
        ':hover': {
            backgroundColor: colors.bloodBright,
            color: '#fff8e8',
            borderColor: colors.goldBright,
        },
    },
    large: {
        minHeight: '3rem',
        fontSize: '0.95rem',
        padding: '0 2rem',
    },
    wide: {
        width: '100%',
    },
    ghost: {
        borderColor: 'transparent',
        backgroundColor: 'transparent',
        ':hover': {
            borderColor: colors.goldHair,
            backgroundColor: 'rgba(58, 40, 25, 0.45)',
        },
    },
    field: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.3rem',
        minWidth: 0,
    },
    label: {
        display: 'flex',
        justifyContent: 'space-between',
        fontFamily: fonts.heading,
        fontSize: '0.66rem',
        fontWeight: 700,
        letterSpacing: '0.22em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    hint: {
        fontFamily: fonts.body,
        letterSpacing: '0.04em',
        textTransform: 'none',
        fontWeight: 400,
        fontStyle: 'italic',
        color: colors.faint,
    },
    input: {
        appearance: 'none',
        width: '100%',
        minWidth: 0,
        height: { default: '2.3rem', '@media (max-height: 620px)': '2.05rem' },
        padding: '0 0.7rem',
        borderWidth: '0 0 1px 0',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        backgroundColor: 'rgba(0, 0, 0, 0.35)',
        color: colors.cream,
        fontFamily: fonts.body,
        fontSize: '1.12rem',
        letterSpacing: '0.02em',
        outline: 'none',
        caretColor: colors.goldBright,
        transitionProperty: 'border-color, background-color, box-shadow',
        transitionDuration: motion.quick,
        '::placeholder': {
            color: 'rgba(168, 146, 109, 0.55)',
        },
        ':focus': {
            borderColor: colors.goldBright,
            backgroundColor: 'rgba(0, 0, 0, 0.5)',
            boxShadow: `0 1px 0 ${colors.goldBright}`,
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
        padding: '0.55rem 0',
        borderWidth: '0 0 1px 0',
        borderStyle: 'solid',
        borderColor: colors.goldHair,
        backgroundColor: 'transparent',
        color: colors.parchment,
        fontFamily: fonts.body,
        fontSize: '1rem',
        textAlign: 'left',
        outline: 'none',
        ':focus-visible': {
            color: colors.goldBright,
        },
    },
    switchTrack: {
        position: 'relative',
        flexShrink: 0,
        width: '2.4rem',
        height: '1.2rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.goldLine,
        backgroundColor: 'rgba(0, 0, 0, 0.4)',
        transitionProperty: 'background-color',
        transitionDuration: motion.quick,
    },
    switchOn: {
        backgroundColor: colors.blood,
    },
    switchKnob: {
        position: 'absolute',
        top: '0.12rem',
        left: '0.12rem',
        width: '0.85rem',
        height: '0.85rem',
        backgroundColor: colors.parchment,
        transform: 'rotate(45deg) scale(0.8)',
        transitionProperty: 'left',
        transitionDuration: motion.quick,
    },
    switchKnobOn: {
        left: '1.3rem',
        backgroundColor: colors.goldBright,
    },
    stepper: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        fontFamily: fonts.body,
        color: colors.cream,
    },
    stepButton: {
        minHeight: '1.8rem',
        minWidth: '1.8rem',
        padding: 0,
        letterSpacing: 0,
    },
    stepValue: {
        minWidth: '4.2rem',
        textAlign: 'center',
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
    return (
        <div {...stylex.props(styles.divider)} aria-hidden="true">
            <span {...stylex.props(styles.dividerLine)} />◆<span {...stylex.props(styles.dividerLine)} />
        </div>
    );
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
