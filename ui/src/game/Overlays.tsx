import * as stylex from '@stylexjs/stylex';
import { useEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Heading, Panel } from '../components/ui';
import { pauseOpen, roster, state } from '../store';
import { colors, fonts, motion } from '../tokens.stylex';
import { Roster } from './Roster';

const styles = stylex.create({
    center: {
        position: 'absolute',
        inset: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        pointerEvents: 'none',
    },
    dim: {
        backgroundColor: 'rgba(6, 4, 3, 0.6)',
        backgroundImage: 'radial-gradient(ellipse at center, rgba(0, 0, 0, 0) 30%, rgba(0, 0, 0, 0.7) 100%)',
    },
    scoreboard: {
        width: '32rem',
        maxWidth: '90vw',
    },
    meta: {
        display: 'flex',
        justifyContent: 'space-between',
        marginBottom: '0.6rem',
        fontFamily: fonts.body,
        fontSize: '0.8rem',
        color: colors.muted,
    },
    pause: {
        display: 'grid',
        gridTemplateColumns: {
            default: '17rem 22rem',
            '@media (max-width: 760px)': '17rem',
        },
        gap: '1.6rem',
    },
    side: {
        display: {
            default: 'block',
            '@media (max-width: 760px)': 'none',
        },
    },
    items: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.3rem',
    },
    item: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.7rem',
        padding: '0.55rem 0.8rem',
        fontFamily: fonts.heading,
        fontWeight: 700,
        fontSize: '0.9rem',
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
        color: colors.parchment,
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: 'transparent',
        transitionProperty: 'background-color, color, border-color',
        transitionDuration: motion.quick,
    },
    itemActive: {
        color: colors.cream,
        borderColor: colors.gold,
        backgroundColor: colors.blood,
        backgroundImage: 'linear-gradient(90deg, rgba(0, 0, 0, 0.25), rgba(0, 0, 0, 0))',
    },
    marker: {
        width: '0.45rem',
        height: '0.45rem',
        transform: 'rotate(45deg)',
        backgroundColor: 'transparent',
    },
    markerActive: {
        backgroundColor: colors.goldBright,
    },
    hint: {
        marginTop: '1rem',
        fontFamily: fonts.body,
        fontSize: '0.72rem',
        color: colors.muted,
        textAlign: 'center',
    },
    confirm: {
        color: '#f7d9c9',
    },
});

export function Scoreboard() {
    const { mission, players } = roster.value;
    const connection = state.value.connection;
    return (
        <div {...stylex.props(styles.center)}>
            <Panel style={styles.scoreboard}>
                <Heading>Who's in town</Heading>
                <div {...stylex.props(styles.meta)}>
                    <span>
                        {connection.host}:{connection.port}
                    </span>
                    <span>
                        {mission || 'Lost Heaven'} · {players.length} {players.length === 1 ? 'player' : 'players'}
                    </span>
                </div>
                <Roster />
            </Panel>
        </div>
    );
}

type Action = 'resume' | 'disconnect' | 'quit';

const actions: { id: Action; label: string }[] = [
    { id: 'resume', label: 'Resume' },
    { id: 'disconnect', label: 'Disconnect' },
    { id: 'quit', label: 'Quit to desktop' },
];

export function PauseMenu() {
    const [selected, setSelected] = useState(0);
    const [confirm, setConfirm] = useState<Action | null>(null);
    const runRef = useRef<((action: Action) => void) | null>(null);

    useEffect(() => {
        const close = () => {
            pauseOpen.value = false;
            send('pause:close');
        };
        const run = (action: Action) => {
            if (action === 'resume') {
                close();
                return;
            }
            if (confirm !== action) {
                setConfirm(action);
                return;
            }
            pauseOpen.value = false;
            send(action === 'disconnect' ? 'menu:disconnect' : 'app:quit');
        };
        runRef.current = run;
        const onKey = (event: KeyboardEvent) => {
            if (event.key === 'ArrowUp' || event.key === 'ArrowDown' || event.key === 'Tab') {
                event.preventDefault();
                const delta = event.key === 'ArrowUp' || (event.key === 'Tab' && event.shiftKey) ? -1 : 1;
                setSelected((value) => (value + delta + actions.length) % actions.length);
                setConfirm(null);
            } else if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                run(actions[selected].id);
            }
        };
        window.addEventListener('keydown', onKey);
        return () => window.removeEventListener('keydown', onKey);
    }, [selected, confirm]);

    return (
        <div {...stylex.props(styles.center, styles.dim)}>
            <div {...stylex.props(styles.pause)}>
                <Panel>
                    <Heading>Paused</Heading>
                    <div {...stylex.props(styles.items)}>
                        {actions.map((action, index) => (
                            <div
                                key={action.id}
                                {...stylex.props(styles.item, index === selected && styles.itemActive)}
                                onMouseEnter={() => {
                                    if (index !== selected) {
                                        setSelected(index);
                                        setConfirm(null);
                                    }
                                }}
                                onClick={() => runRef.current?.(action.id)}
                            >
                                <span {...stylex.props(styles.marker, index === selected && styles.markerActive)} />
                                {confirm === action.id ? 'Sure? Enter' : action.label}
                            </div>
                        ))}
                    </div>
                    <div {...stylex.props(styles.hint, confirm !== null && styles.confirm)}>
                        {confirm ? 'Press Enter or click again to confirm' : '↑ ↓ choose · Enter select · Esc resume'}
                    </div>
                </Panel>
                <Panel style={styles.side} delay={100}>
                    <Heading>The family</Heading>
                    <Roster />
                </Panel>
            </div>
        </div>
    );
}
