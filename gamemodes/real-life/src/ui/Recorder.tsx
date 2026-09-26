import * as stylex from '@stylexjs/stylex';
import { useEffect, useLayoutEffect, useState } from 'preact/hooks';
import type { RecorderState } from '../shared/camera';
import { EVENT, isRecord } from '../shared/protocol';
import type en from '../shared/locales/en.json';
import { send } from './bridge';
import { theme } from './tokens.stylex';
import { recorderKey } from './recorder-key';

const movementKeys = new Set([
    'KeyW',
    'KeyA',
    'KeyS',
    'KeyD',
    'KeyQ',
    'KeyE',
    'ArrowUp',
    'ArrowDown',
    'ArrowLeft',
    'ArrowRight',
    'ShiftLeft',
    'ShiftRight',
    'AltLeft',
    'AltRight',
    'Space',
]);
const shortcuts: Record<string, string> = {
    F2: 'point',
    F3: 'new',
    F5: 'play',
    F6: 'save',
    Backspace: 'undo',
};

export function useRecorder(enabled: boolean) {
    const [state, setState] = useState<RecorderState | null>(null);
    const active = enabled && state?.active === true;

    useEffect(() => {
        const receive = (event: Event) => {
            const value: unknown = (event as CustomEvent).detail;

            if (isRecord(value) && typeof value.active === 'boolean' && isRecord(value.position)) {
                setState(value as unknown as RecorderState);
            }
        };

        window.addEventListener(EVENT.recorderState, receive);

        return () => window.removeEventListener(EVENT.recorderState, receive);
    }, []);

    useLayoutEffect(() => {
        if (!enabled) {
            return;
        }

        const keys = new Set<string>();
        let previous: { x: number; y: number; action: 'look' | 'roll' } | null = null;
        const update = () => send(EVENT.recorder, { action: 'keys', keys: [...keys] });
        const down = (event: KeyboardEvent) => {
            const key = recorderKey(event);

            if (key === 'F4') {
                event.preventDefault();

                if (!event.repeat) {
                    send(EVENT.recorder, { action: 'toggle' });
                }

                return;
            }

            if (!active || event.target instanceof HTMLInputElement) {
                return;
            }

            if (shortcuts[key]) {
                event.preventDefault();

                if (!event.repeat) {
                    send(EVENT.recorder, { action: shortcuts[key] });
                }
            } else if (movementKeys.has(key)) {
                event.preventDefault();
                keys.add(key);
                update();
            }
        };
        const up = (event: KeyboardEvent) => {
            if (keys.delete(recorderKey(event))) {
                event.preventDefault();
                update();
            }
        };
        const clear = () => {
            keys.clear();
            previous = null;
            update();
        };
        const mouse = (event: MouseEvent) => {
            const action = keys.has('Space') ? 'roll' : event.buttons & 2 ? 'look' : null;

            if (!active || !action) {
                previous = null;

                return;
            }

            if (previous?.action === action) {
                send(EVENT.recorder, {
                    action,
                    x: event.clientX - previous.x,
                    ...(action === 'look' ? { y: event.clientY - previous.y } : {}),
                });
            }

            previous = { x: event.clientX, y: event.clientY, action };
        };
        const context = (event: Event) => {
            if (active) {
                event.preventDefault();
            }
        };

        window.addEventListener('keydown', down);
        window.addEventListener('keyup', up);
        window.addEventListener('blur', clear);
        window.addEventListener('mousemove', mouse);
        window.addEventListener('contextmenu', context);

        return () => {
            clear();
            window.removeEventListener('keydown', down);
            window.removeEventListener('keyup', up);
            window.removeEventListener('blur', clear);
            window.removeEventListener('mousemove', mouse);
            window.removeEventListener('contextmenu', context);
        };
    }, [enabled, active]);

    return { state, active };
}

export function Recorder({ state, t }: { state: RecorderState; t: typeof en.recorder }) {
    const command = (action: string) => send(EVENT.recorder, { action });

    return (
        <div {...stylex.props(styles.root)}>
            <header {...stylex.props(styles.panel)}>
                <div {...stylex.props(styles.row)}>
                    <strong>{t.title}</strong>
                    <span>
                        {t.spline} {state.spline} · {state.points} {t.points}
                    </span>
                    <span>{state.dirty ? t.unsaved : t.saved}</span>
                    <button {...stylex.props(styles.button)} onClick={() => command('toggle')}>
                        F4 · {t.close}
                    </button>
                </div>
                <p {...stylex.props(styles.help)}>{t.movement}</p>
                <p {...stylex.props(styles.help)}>{t.look}</p>
            </header>
            <footer {...stylex.props(styles.panel)}>
                <div {...stylex.props(styles.row)}>
                    <button {...stylex.props(styles.button)} onClick={() => command('point')}>
                        F2 · {t.add}
                    </button>
                    <button {...stylex.props(styles.button)} onClick={() => command('new')}>
                        F3 · {t.new}
                    </button>
                    <button {...stylex.props(styles.button)} onClick={() => command('play')}>
                        F5 · {state.replaying ? t.stop : t.play}
                    </button>
                    <button {...stylex.props(styles.button)} onClick={() => command('save')}>
                        F6 · {t.save}
                    </button>
                    <button {...stylex.props(styles.button)} onClick={() => command('undo')}>
                        {t.undo}
                    </button>
                    <label {...stylex.props(styles.duration)}>
                        {t.duration}
                        <input
                            {...stylex.props(styles.input)}
                            type="number"
                            min="3"
                            max="180"
                            value={state.seconds}
                            onChange={(event) =>
                                send(EVENT.recorder, {
                                    action: 'duration',
                                    seconds: Number(event.currentTarget.value),
                                })
                            }
                        />
                    </label>
                </div>
                <div {...stylex.props(styles.row, styles.help)}>
                    <span role="status">{t.status[state.status]}</span>
                    <span>
                        {t.position}: {state.position.x.toFixed(1)}, {state.position.y.toFixed(1)},{' '}
                        {state.position.z.toFixed(1)}
                    </span>
                    <span>
                        {t.roll}: {((state.roll * 180) / Math.PI).toFixed(1)}°
                    </span>
                </div>
            </footer>
        </div>
    );
}

const styles = stylex.create({
    root: {
        position: 'fixed',
        inset: 0,
        display: 'flex',
        flexDirection: 'column',
        justifyContent: 'space-between',
        padding: 20,
        color: theme.paper,
        fontFamily: theme.body,
        fontSize: 12,
    },
    panel: {
        backgroundColor: theme.panel,
        padding: 16,
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        borderRadius: theme.radius,
    },
    row: {
        display: 'flex',
        flexWrap: 'wrap',
        alignItems: 'center',
        gap: 14,
        justifyContent: 'space-between',
    },
    help: { color: theme.muted, marginTop: 10, marginBottom: 0, lineHeight: 1.5, fontSize: 11 },
    button: {
        color: theme.brass,
        backgroundColor: 'transparent',
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        borderRadius: theme.radius,
        padding: '9px 12px',
        cursor: 'pointer',
        fontFamily: theme.body,
        fontSize: 11,
        ':hover': { backgroundColor: theme.field },
    },
    duration: { display: 'flex', alignItems: 'center', gap: 8, color: theme.muted },
    input: {
        width: 60,
        padding: 6,
        color: theme.paper,
        backgroundColor: theme.field,
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        fontFamily: theme.body,
    },
});
