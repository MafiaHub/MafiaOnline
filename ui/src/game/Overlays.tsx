import * as stylex from '@stylexjs/stylex';
import { useLayoutEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Heading, Panel } from '../components/ui';
import { SettingsScreen } from '../settings/SettingsScreen';
import { pauseOpen, roster, settingsOpen, state } from '../store';
import { colors } from '../tokens.stylex';
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
        backgroundColor: 'rgba(6, 6, 7, 0.66)',
    },
    scoreboard: {
        width: '32rem',
        maxWidth: '90vw',
    },
    meta: {
        display: 'flex',
        justifyContent: 'space-between',
        marginBottom: '0.75rem',
        fontSize: '0.8rem',
        color: colors.muted,
    },
    pause: {
        pointerEvents: 'auto',
        display: 'grid',
        gridTemplateColumns: {
            default: '17rem 22rem',
            '@media (max-width: 760px)': '17rem',
        },
        gap: '1.5rem',
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
        gap: '0.125rem',
    },
    item: {
        appearance: 'none',
        backgroundColor: 'transparent',
        textAlign: 'left',
        cursor: 'pointer',
        display: 'flex',
        alignItems: 'center',
        gap: '0.75rem',
        padding: '0.625rem 0.75rem 0.625rem 0',
        fontWeight: 600,
        fontSize: '0.8rem',
        letterSpacing: '0.18em',
        textTransform: 'uppercase',
        color: colors.muted,
        borderWidth: 0,
        outline: 'none',
    },
    itemActive: {
        color: colors.text,
        backgroundColor: colors.raised,
    },
    // The red bar every selected item carries.
    marker: {
        alignSelf: 'stretch',
        width: '3px',
        backgroundColor: 'transparent',
    },
    markerActive: {
        backgroundColor: colors.accent,
    },
    hint: {
        marginTop: '1rem',
        fontSize: '0.75rem',
        color: colors.muted,
        textAlign: 'center',
    },
    confirm: {
        color: colors.danger,
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

type Action = 'resume' | 'settings' | 'disconnect' | 'quit';

const actions: { id: Action; label: string }[] = [
    { id: 'resume', label: 'Resume' },
    { id: 'settings', label: 'Settings' },
    { id: 'disconnect', label: 'Disconnect' },
    { id: 'quit', label: 'Quit to desktop' },
];

export function PauseMenu() {
    const [selected, setSelected] = useState(0);
    const [confirm, setConfirm] = useState<Action | null>(null);
    const runRef = useRef<((action: Action) => void) | null>(null);

    useLayoutEffect(() => {
        const close = () => {
            pauseOpen.value = false;
            send('pause:close');
        };
        const run = (action: Action) => {
            if (action === 'resume') {
                close();
                return;
            }
            if (action === 'settings') {
                setConfirm(null);
                settingsOpen.value = true;
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
            // The settings screen handles its own keys.
            if (settingsOpen.value) {
                return;
            }
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

    if (settingsOpen.value) {
        return (
            <div {...stylex.props(styles.center, styles.dim)}>
                <SettingsScreen onClose={() => (settingsOpen.value = false)} />
            </div>
        );
    }
    return (
        <div {...stylex.props(styles.center, styles.dim)}>
            <div {...stylex.props(styles.pause)}>
                <Panel>
                    <Heading>Paused</Heading>
                    <div {...stylex.props(styles.items)}>
                        {actions.map((action, index) => (
                            <button
                                type="button"
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
                            </button>
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
