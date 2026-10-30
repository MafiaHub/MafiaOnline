import * as stylex from '@stylexjs/stylex';
import type { ComponentChildren } from 'preact';
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Button, Heading, Panel, Toggle } from '../components/ui';
import { type GameSettings, type Preferences, type SettingsSlot, defaultPreferences, gameSettings, preferences, voiceLevel } from '../store';
import { colors, radii } from '../tokens.stylex';
import { keyNameFromEvent, keyNameFromMouse } from './keys';

// The selection marker shared with the rest of the UI: a red bar on the left.
const marker = `inset 3px 0 0 ${colors.accent}`;

const styles = stylex.create({
    panel: {
        width: '54rem',
        maxWidth: '94vw',
        height: '86vh',
        maxHeight: '46rem',
        display: 'flex',
        flexDirection: 'column',
        pointerEvents: 'auto',
    },
    tabs: {
        display: 'flex',
        flexWrap: 'wrap',
        gap: '0.375rem',
        marginBottom: '1rem',
    },
    tab: {
        minHeight: '2rem',
        padding: '0 0.875rem',
        fontSize: '0.7rem',
    },
    tabActive: {
        borderColor: colors.text,
        backgroundColor: colors.raised,
        boxShadow: marker,
    },
    body: {
        flexGrow: 1,
        minHeight: 0,
        overflowY: 'auto',
        paddingRight: '0.5rem',
    },
    note: {
        margin: '0 0 1rem',
        fontSize: '0.85rem',
        lineHeight: 1.5,
        color: colors.muted,
    },
    section: {
        marginBottom: '1.5rem',
    },
    sectionTitle: {
        margin: '0 0 0.25rem',
        paddingBottom: '0.375rem',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.line,
        fontWeight: 600,
        fontSize: '0.68rem',
        letterSpacing: '0.18em',
        textTransform: 'uppercase',
        color: colors.text,
    },
    row: {
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) 9.5rem 9.5rem',
        alignItems: 'center',
        gap: '0.5rem',
        padding: '0.25rem 0',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.border,
    },
    rowLabel: {
        fontSize: '0.92rem',
        color: colors.text,
        minWidth: 0,
    },
    conflict: {
        display: 'block',
        fontSize: '0.75rem',
        color: colors.danger,
    },
    slot: {
        appearance: 'none',
        position: 'relative',
        height: '1.875rem',
        minWidth: '8rem',
        padding: '0 0.5rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.border,
        borderRadius: radii.sm,
        backgroundColor: colors.bg,
        color: colors.text,
        fontSize: '0.875rem',
        whiteSpace: 'nowrap',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        cursor: 'pointer',
        outline: 'none',
        ':hover': {
            borderColor: colors.line,
            backgroundColor: colors.raised,
        },
        ':focus-visible': {
            borderColor: colors.line,
            boxShadow: marker,
        },
    },
    slotEmpty: {
        color: colors.faint,
    },
    slotChanged: {
        fontWeight: 600,
    },
    slotCapturing: {
        borderColor: colors.accent,
        backgroundColor: colors.accentSoft,
        boxShadow: marker,
    },
    slotConflict: {
        borderColor: colors.danger,
    },
    none: {
        visibility: 'hidden',
    },
    capture: {
        position: 'absolute',
        inset: 0,
        zIndex: 30,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        borderRadius: radii.md,
        backgroundColor: colors.scrim,
        pointerEvents: 'auto',
    },
    captureBox: {
        width: '26rem',
        maxWidth: '90vw',
        textAlign: 'center',
    },
    captureText: {
        margin: '0 0 0.5rem',
        fontSize: '1rem',
        color: colors.text,
    },
    captureHint: {
        margin: '0 0 1rem',
        fontSize: '0.85rem',
        color: colors.muted,
    },
    captureActions: {
        display: 'flex',
        justifyContent: 'center',
        gap: '0.5rem',
    },
    field: {
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) 13rem 3.5rem',
        alignItems: 'center',
        gap: '0.75rem',
        padding: '0.5rem 0',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.border,
        fontSize: '0.92rem',
        color: colors.text,
    },
    fieldWide: {
        gridTemplateColumns: 'minmax(0, 1fr) auto',
    },
    range: {
        width: '100%',
        accentColor: colors.accent,
    },
    value: {
        textAlign: 'right',
        color: colors.muted,
        fontVariantNumeric: 'tabular-nums',
    },
    segmented: {
        display: 'flex',
        gap: '0.25rem',
    },
    chip: {
        minHeight: '1.75rem',
        padding: '0 0.75rem',
        fontSize: '0.66rem',
    },
    select: {
        appearance: 'none',
        maxWidth: '22rem',
        height: '2rem',
        padding: '0 0.625rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.border,
        borderRadius: radii.sm,
        backgroundColor: colors.bg,
        color: colors.text,
        fontSize: '0.875rem',
        outline: 'none',
        ':focus': {
            borderColor: colors.line,
            boxShadow: marker,
        },
    },
    meterTrack: {
        position: 'relative',
        height: '0.625rem',
        marginTop: '0.5rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.border,
        backgroundColor: colors.bg,
        overflow: 'hidden',
    },
    meterFill: {
        position: 'absolute',
        top: 0,
        bottom: 0,
        left: 0,
        backgroundColor: colors.muted,
    },
    meterLive: {
        backgroundColor: colors.success,
    },
    meterMark: {
        position: 'absolute',
        top: 0,
        bottom: 0,
        width: '2px',
        backgroundColor: colors.accentHover,
    },
    footer: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.75rem',
        marginTop: '1rem',
    },
    spacer: {
        flexGrow: 1,
    },
    hint: {
        fontSize: '0.8rem',
        color: colors.muted,
    },
});

type Tab = 'controls' | 'gameplay' | 'audio' | 'voice' | 'interface';

const tabs: { id: Tab; label: string }[] = [
    { id: 'controls', label: 'Controls' },
    { id: 'gameplay', label: 'Gameplay' },
    { id: 'audio', label: 'Audio' },
    { id: 'voice', label: 'Voice' },
    { id: 'interface', label: 'Interface' },
];

const percent = (value: number) => `${Math.round(value * 100)}%`;

// Sliders preview live while dragged; the client applies each value, so the
// stream is thinned to one message every 80 ms with the final one always sent.
function Slider(props: { label: string; value: number; min: number; max: number; step: number; format: (value: number) => string; onChange: (value: number) => void }) {
    const [local, setLocal] = useState<number | null>(null);
    const last = useRef(0);
    const shown = local ?? props.value;
    const commit = (value: number, final: boolean) => {
        const now = Date.now();
        if (final || now - last.current >= 80) {
            last.current = now;
            props.onChange(value);
        }
        if (final) setLocal(null);
    };
    return (
        <label {...stylex.props(styles.field)}>
            <span>{props.label}</span>
            <input
                type="range"
                {...stylex.props(styles.range)}
                min={props.min}
                max={props.max}
                step={props.step}
                value={shown}
                onInput={(event) => {
                    const value = Number((event.currentTarget as HTMLInputElement).value);
                    setLocal(value);
                    commit(value, false);
                }}
                onChange={(event) => commit(Number((event.currentTarget as HTMLInputElement).value), true)}
            />
            <span {...stylex.props(styles.value)}>{props.format(shown)}</span>
        </label>
    );
}

function Segmented<T extends string | number>(props: { label: string; value: T; options: { value: T; label: string }[]; onChange: (value: T) => void }) {
    return (
        <div {...stylex.props(styles.field, styles.fieldWide)}>
            <span>{props.label}</span>
            <span {...stylex.props(styles.segmented)}>
                {props.options.map((option) => (
                    <Button key={String(option.value)} style={[styles.chip, props.value === option.value && styles.tabActive]} onClick={() => props.onChange(option.value)}>
                        {option.label}
                    </Button>
                ))}
            </span>
        </div>
    );
}

function Section(props: { title: string; children: ComponentChildren }) {
    return (
        <div {...stylex.props(styles.section)}>
            <h3 {...stylex.props(styles.sectionTitle)}>{props.title}</h3>
            {props.children}
        </div>
    );
}

function SlotButton(props: { id: string; slot: SettingsSlot; capturing: boolean; onCapture: (id: string) => void }) {
    const { slot } = props;
    const changed = slot.label !== slot.defaultLabel;
    return (
        <button
            type="button"
            title={slot.defaultLabel ? `Default: ${slot.defaultLabel}` : 'Unbound by default'}
            {...stylex.props(styles.slot, !slot.label && styles.slotEmpty, changed && styles.slotChanged, slot.conflicts.length > 0 && styles.slotConflict, props.capturing && styles.slotCapturing)}
            onClick={() => props.onCapture(props.id)}
        >
            {props.capturing ? 'Press a key…' : slot.label || 'Unbound'}
        </button>
    );
}

function Capture(props: { slot: SettingsSlot; label: string; error: string | null; onClear: () => void; onCancel: () => void }) {
    const devices = props.slot.mouse ? (props.slot.wheel ? 'a key, a mouse button or the wheel' : 'a key or a mouse button') : 'a key';
    return (
        <div {...stylex.props(styles.capture)}>
            <Panel style={styles.captureBox}>
                <p {...stylex.props(styles.captureText)}>
                    Press {devices} for <b>{props.label}</b>
                </p>
                <p {...stylex.props(styles.captureHint)}>{props.error ?? 'Escape cancels.'}</p>
                <div {...stylex.props(styles.captureActions)} data-capture-ignore>
                    {props.slot.clearable && <Button onClick={props.onClear}>Unbind</Button>}
                    <Button onClick={props.onCancel}>Cancel</Button>
                </div>
            </Panel>
        </div>
    );
}

function ControlsPage(props: { settings: GameSettings; capturing: string | null; onCapture: (id: string) => void }) {
    const { settings } = props;
    return (
        <>
            <p {...stylex.props(styles.note)}>
                Click a key to change it. Changes apply at once and are kept for Mafia Online only; your Mafia profile is not changed.
            </p>
            {settings.groups.map((group) => (
                <Section key={group.id} title={group.label}>
                    {group.rows.map((row) => {
                        const conflicts = [...new Set(row.slots.flatMap((id) => settings.slots[id]?.conflicts ?? []))];
                        return (
                            <div key={row.id} {...stylex.props(styles.row)}>
                                <span {...stylex.props(styles.rowLabel)}>
                                    {row.label}
                                    {conflicts.length > 0 && <span {...stylex.props(styles.conflict)}>Also: {conflicts.join(', ')}</span>}
                                </span>
                                {[0, 1].map((index) => {
                                    const id = row.slots[index];
                                    const slot = id ? settings.slots[id] : undefined;
                                    return slot ? (
                                        <SlotButton key={id} id={id} slot={slot} capturing={props.capturing === id} onCapture={props.onCapture} />
                                    ) : (
                                        <span key={index} {...stylex.props(styles.none)} />
                                    );
                                })}
                            </div>
                        );
                    })}
                </Section>
            ))}
        </>
    );
}

function GameplayPage(props: { settings: GameSettings }) {
    const { mouse, hud, toggles } = props.settings;
    const setMouse = (values: Partial<GameSettings['mouse']>) => send('settings:set', { mouse: values });
    const setHud = (values: Partial<GameSettings['hud']>) => send('settings:set', { hud: values });
    return (
        <>
            {props.settings.profile.controls && <p {...stylex.props(styles.note)}>The mouse and HUD values come from your Mafia profile until you change one here.</p>}
            <Section title="Mouse">
                <Slider label="Look sensitivity, horizontal" value={mouse.sensitivityX} min={0.05} max={1} step={0.01} format={percent} onChange={(value) => setMouse({ sensitivityX: value })} />
                <Slider label="Look sensitivity, vertical" value={mouse.sensitivityY} min={0.05} max={1} step={0.01} format={percent} onChange={(value) => setMouse({ sensitivityY: value })} />
                <Slider label="Turning speed limit" value={mouse.turnLimit} min={0.05} max={1} step={0.01} format={percent} onChange={(value) => setMouse({ turnLimit: value })} />
                <Toggle label="Invert vertical look" value={mouse.invertY} onChange={(value) => setMouse({ invertY: value })} />
            </Section>
            <Section title="Driving and movement">
                <Slider label="Keyboard steering linearity" value={mouse.steering} min={0} max={1} step={0.01} format={percent} onChange={(value) => setMouse({ steering: value })} />
                <Toggle label="Double-tap a side to roll" value={mouse.doubleTapRoll} onChange={(value) => setMouse({ doubleTapRoll: value })} />
            </Section>
            <Section title="HUD">
                <Segmented
                    label="Crosshair"
                    value={mouse.crosshair}
                    options={[
                        { value: 0, label: 'Off' },
                        { value: 1, label: 'Dot' },
                        { value: 2, label: 'Cross' },
                    ]}
                    onChange={(value) => setMouse({ crosshair: value })}
                />
                <Segmented
                    label="Speedometer"
                    value={mouse.mph ? 'mph' : 'kmh'}
                    options={[
                        { value: 'mph', label: 'mph' },
                        { value: 'kmh', label: 'km/h' },
                    ]}
                    onChange={(value) => setMouse({ mph: value === 'mph' })}
                />
                <Toggle label="Player names above heads" value={hud.nametags} onChange={(value) => setHud({ nametags: value })} />
                <Toggle label="Health bars above heads" value={hud.nametagHealth} onChange={(value) => setHud({ nametagHealth: value })} />
            </Section>
            {toggles.length > 0 && (
                <Section title="Server">
                    {toggles.map((toggle) => (
                        <Toggle
                            key={`${toggle.resource}/${toggle.id}`}
                            label={toggle.label}
                            value={toggle.value}
                            onChange={(value) => send('settings:toggle', { resource: toggle.resource, id: toggle.id, value })}
                        />
                    ))}
                </Section>
            )}
        </>
    );
}

function AudioPage(props: { settings: GameSettings }) {
    const { audio, media, voice } = props.settings;
    const setAudio = (values: Partial<GameSettings['audio']>) => send('settings:set', { audio: values });
    return (
        <>
            {props.settings.profile.audio && <p {...stylex.props(styles.note)}>The game volumes come from your Mafia profile until you change one here.</p>}
            <Section title="Game">
                <Slider label="Master (all game sound)" value={audio.sound} min={0} max={1} step={0.01} format={percent} onChange={(value) => setAudio({ sound: value })} />
                <Slider label="Music" value={audio.music} min={0} max={1} step={0.01} format={percent} onChange={(value) => setAudio({ music: value })} />
                <Slider label="Cars" value={audio.car} min={0} max={1} step={0.01} format={percent} onChange={(value) => setAudio({ car: value })} />
                <Slider label="Speech" value={audio.speech} min={0} max={1} step={0.01} format={percent} onChange={(value) => setAudio({ speech: value })} />
            </Section>
            <Section title="Multiplayer">
                <Slider label="Voice chat" value={voice.volume} min={0} max={2} step={0.01} format={percent} onChange={(value) => send('settings:set', { voice: { volume: value } })} />
                <Slider label="Car radio and server pages" value={media} min={0} max={1} step={0.01} format={percent} onChange={(value) => send('settings:set', { media: value })} />
            </Section>
        </>
    );
}

function VoicePage(props: { settings: GameSettings; capturing: string | null; onCapture: (id: string) => void }) {
    const { voice, devices, voiceAvailable, slots } = props.settings;
    const level = voiceLevel.value;
    const setVoice = (values: Partial<GameSettings['voice']>) => send('settings:set', { voice: values });
    useEffect(() => {
        send('settings:meter', { on: true });
        return () => send('settings:meter', { on: false });
    }, []);
    if (!voiceAvailable) {
        return <p {...stylex.props(styles.note)}>Voice chat is not available in this client.</p>;
    }
    const pushToTalk = slots['mod:pushToTalk'];
    // The meter is drawn on a square-root scale so quiet speech still shows.
    const scale = (value: number) => `${Math.min(100, Math.sqrt(Math.max(0, value)) * 100)}%`;
    const known = voice.device === '' || devices.includes(voice.device);
    return (
        <>
            <Section title="Voice chat">
                <Toggle label="Voice chat" value={voice.enabled} onChange={(value) => setVoice({ enabled: value })} />
                <Segmented
                    label="Talk with"
                    value={voice.mode}
                    options={[
                        { value: 'pushToTalk', label: 'Push to talk' },
                        { value: 'voiceActivity', label: 'Voice activity' },
                    ]}
                    onChange={(value) => setVoice({ mode: value })}
                />
                {voice.mode === 'pushToTalk' && pushToTalk && (
                    <div {...stylex.props(styles.field, styles.fieldWide)}>
                        <span>Push-to-talk key</span>
                        <SlotButton id="mod:pushToTalk" slot={pushToTalk} capturing={props.capturing === 'mod:pushToTalk'} onCapture={props.onCapture} />
                    </div>
                )}
                {voice.mode === 'pushToTalk' && (
                    <Slider label="Keep sending after release" value={voice.releaseMs} min={0} max={1000} step={10} format={(value) => `${Math.round(value)} ms`} onChange={(value) => setVoice({ releaseMs: Math.round(value) })} />
                )}
                {voice.mode === 'voiceActivity' && (
                    <Slider label="Activation level" value={voice.threshold} min={0.005} max={0.25} step={0.005} format={(value) => `${Math.round(value * 1000) / 10}%`} onChange={(value) => setVoice({ threshold: value })} />
                )}
                <Toggle label="Noise suppression" value={voice.noiseSuppression} onChange={(value) => setVoice({ noiseSuppression: value })} />
            </Section>
            <Section title="Microphone">
                <div {...stylex.props(styles.field, styles.fieldWide)}>
                    <span>Device</span>
                    <span {...stylex.props(styles.segmented)}>
                        <select {...stylex.props(styles.select)} value={voice.device} onChange={(event) => setVoice({ device: (event.currentTarget as HTMLSelectElement).value })}>
                            <option value="">System default</option>
                            {!known && <option value={voice.device}>{voice.device} (not connected)</option>}
                            {devices.map((device) => (
                                <option key={device} value={device}>
                                    {device}
                                </option>
                            ))}
                        </select>
                        <Button style={styles.chip} onClick={() => send('settings:devices')}>
                            Refresh
                        </Button>
                    </span>
                </div>
                <div {...stylex.props(styles.section)}>
                    <span {...stylex.props(styles.hint)}>
                        {level.microphone
                            ? level.talking
                                ? 'Others can hear you now.'
                                : voice.mode === 'voiceActivity'
                                  ? 'Speak: the bar turns green past the mark.'
                                  : 'Speak to test the level; hold your push-to-talk key to send.'
                            : 'The microphone opens once you are on a server with voice chat.'}
                    </span>
                    <div {...stylex.props(styles.meterTrack)}>
                        <div {...stylex.props(styles.meterFill, level.talking && styles.meterLive)} style={{ width: scale(level.level) }} />
                        {voice.mode === 'voiceActivity' && <div {...stylex.props(styles.meterMark)} style={{ left: scale(voice.threshold) }} />}
                    </div>
                </div>
            </Section>
        </>
    );
}

function InterfacePage() {
    const prefs = preferences.value;
    const save = <K extends keyof Preferences>(key: K, value: Preferences[K]) => send('settings:save', { ...prefs, [key]: value });
    return (
        <Section title="Chat and menus">
            <Segmented
                label="Chat fades after"
                value={prefs.chatFadeSeconds}
                options={[0, 6, 12, 20, 40].map((seconds) => ({ value: seconds, label: seconds === 0 ? 'Never' : `${seconds}s` }))}
                onChange={(value) => save('chatFadeSeconds', value)}
            />
            <Slider label="Chat text size" value={prefs.chatScale} min={0.8} max={1.5} step={0.05} format={percent} onChange={(value) => save('chatScale', value)} />
            <Slider label="Interface scale" value={prefs.uiScale} min={0.8} max={1.4} step={0.05} format={percent} onChange={(value) => save('uiScale', value)} />
            <Toggle label="Chat timestamps" value={prefs.chatTimestamps} onChange={(value) => save('chatTimestamps', value)} />
            <Toggle label="Reduce motion" value={prefs.reduceMotion} onChange={(value) => save('reduceMotion', value)} />
        </Section>
    );
}

const resets: Record<Tab, string[]> = {
    controls: ['controls'],
    gameplay: ['mouse', 'hud'],
    audio: ['audio'],
    voice: ['voice'],
    interface: [],
};

// The settings screen for the main menu and the pause screen. The client owns
// every value; this page only shows them and sends what the player changes.
export function SettingsScreen(props: { onClose: () => void }) {
    const settings = gameSettings.value;
    const [tab, setTab] = useState<Tab>('controls');
    const [capturing, setCapturing] = useState<string | null>(null);
    const [captureError, setCaptureError] = useState<string | null>(null);
    const [confirmReset, setConfirmReset] = useState(false);
    const closeRef = useRef(props.onClose);
    closeRef.current = props.onClose;

    useEffect(() => {
        send('settings:open');
        return () => send('settings:close');
    }, []);

    const slot = capturing && settings ? settings.slots[capturing] : undefined;
    const stopCapture = () => {
        setCapturing(null);
        setCaptureError(null);
    };
    // A resource that stopped takes its keys with it, mid-capture or not.
    useEffect(() => {
        if (capturing && settings && !slot) stopCapture();
    }, [capturing, settings, slot]);
    const bind = (key: string) => {
        if (capturing) send('settings:bind', { slot: capturing, key });
        stopCapture();
    };

    // Capture phase on the window, ahead of every other handler: while a key
    // is being captured the press is the answer, and Escape backs out one
    // step (capture, reset prompt, then the screen) before the pause menu.
    useLayoutEffect(() => {
        const onKey = (event: KeyboardEvent) => {
            if (event.key === 'Escape' || event.code === 'Escape') {
                event.preventDefault();
                event.stopImmediatePropagation();
                if (capturing) stopCapture();
                else if (confirmReset) setConfirmReset(false);
                else closeRef.current();
                return;
            }
            if (!capturing) return;
            event.preventDefault();
            event.stopImmediatePropagation();
            if (event.repeat) return;
            const name = keyNameFromEvent(event);
            if (!name) {
                setCaptureError('That key cannot be bound. Try another.');
                return;
            }
            bind(name);
        };
        const onMouse = (event: MouseEvent) => {
            if (!capturing || !slot || (event.target as HTMLElement | null)?.closest('[data-capture-ignore]')) return;
            event.preventDefault();
            event.stopImmediatePropagation();
            const name = keyNameFromMouse(event.button);
            if (!name || !slot.mouse) {
                setCaptureError('This one takes a keyboard key.');
                return;
            }
            bind(name);
        };
        const onWheel = (event: WheelEvent) => {
            if (!capturing || !slot || event.deltaY === 0) return;
            event.preventDefault();
            if (!slot.wheel) {
                setCaptureError(slot.mouse ? 'The wheel cannot be held; pick a key or a button.' : 'This one takes a keyboard key.');
                return;
            }
            bind(event.deltaY < 0 ? 'wheelup' : 'wheeldown');
        };
        window.addEventListener('keydown', onKey, true);
        window.addEventListener('mousedown', onMouse, true);
        window.addEventListener('wheel', onWheel, { capture: true, passive: false });
        return () => {
            window.removeEventListener('keydown', onKey, true);
            window.removeEventListener('mousedown', onMouse, true);
            window.removeEventListener('wheel', onWheel, true);
        };
    }, [capturing, confirmReset, slot]);

    const reset = () => {
        if (!confirmReset) {
            setConfirmReset(true);
            return;
        }
        setConfirmReset(false);
        if (tab === 'interface') send('settings:save', defaultPreferences);
        for (const section of resets[tab]) send('settings:reset', { section });
    };

    const owner = capturing && settings ? settings.groups.flatMap((group) => group.rows).find((row) => row.slots.includes(capturing)) : undefined;
    return (
        <Panel style={styles.panel}>
            <Heading>Settings</Heading>
            <div {...stylex.props(styles.tabs)}>
                {tabs.map((entry) => (
                    <Button
                        key={entry.id}
                        style={[styles.tab, tab === entry.id && styles.tabActive]}
                        onClick={() => {
                            setTab(entry.id);
                            setConfirmReset(false);
                            stopCapture();
                        }}
                    >
                        {entry.label}
                    </Button>
                ))}
            </div>
            <div {...stylex.props(styles.body)}>
                {!settings && tab !== 'interface' ? (
                    <p {...stylex.props(styles.note)}>Loading…</p>
                ) : tab === 'controls' && settings ? (
                    <ControlsPage settings={settings} capturing={capturing} onCapture={setCapturing} />
                ) : tab === 'gameplay' && settings ? (
                    <GameplayPage settings={settings} />
                ) : tab === 'audio' && settings ? (
                    <AudioPage settings={settings} />
                ) : tab === 'voice' && settings ? (
                    <VoicePage settings={settings} capturing={capturing} onCapture={setCapturing} />
                ) : (
                    <InterfacePage />
                )}
            </div>
            <div {...stylex.props(styles.footer)}>
                <Button onClick={reset}>{confirmReset ? 'Sure? Click again' : 'Reset page to defaults'}</Button>
                <span {...stylex.props(styles.spacer)} />
                <span {...stylex.props(styles.hint)}>Saved automatically · Esc to go back</span>
                <Button variant="primary" onClick={() => closeRef.current()}>
                    Done
                </Button>
            </div>
            {slot && capturing && <Capture slot={slot} label={owner?.label ?? 'this control'} error={captureError} onClear={() => bind('')} onCancel={stopCapture} />}
        </Panel>
    );
}
