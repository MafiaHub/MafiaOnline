import * as stylex from '@stylexjs/stylex';
import { useState } from 'preact/hooks';
import { send } from '../bridge';
import { Button, Heading, Panel, Stepper, Toggle } from '../components/ui';
import { type Preferences, preferences } from '../store';
import { colors, fonts } from '../tokens.stylex';

const styles = stylex.create({
    scrim: {
        position: 'absolute',
        inset: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        backgroundColor: 'rgba(5, 3, 2, 0.72)',
        zIndex: 20,
    },
    dialog: {
        width: '27rem',
        maxWidth: '92vw',
    },
    text: {
        margin: '0 0 1.2rem',
        textAlign: 'center',
        fontFamily: fonts.body,
        fontStyle: 'italic',
        fontSize: '1.1rem',
        color: colors.parchment,
    },
    actions: {
        display: 'flex',
        justifyContent: 'flex-end',
        gap: '0.7rem',
        marginTop: '1.2rem',
    },
    fade: {
        display: 'flex',
        flexWrap: 'wrap',
        gap: '0.3rem',
        padding: '0.55rem 0',
        borderBottomWidth: '1px',
        borderBottomStyle: 'solid',
        borderBottomColor: colors.goldHair,
    },
    fadeLabel: {
        width: '100%',
        marginBottom: '0.25rem',
        fontFamily: fonts.body,
        fontSize: '1rem',
        color: colors.parchment,
    },
    chip: {
        minHeight: '1.8rem',
        padding: '0 0.7rem',
        fontSize: '0.68rem',
        letterSpacing: '0.1em',
    },
    chipActive: {
        borderColor: colors.gold,
        color: colors.goldBright,
        backgroundColor: 'rgba(77, 13, 15, 0.6)',
    },
});

const fadeChoices = [0, 6, 12, 20, 40];

export function SettingsDialog(props: { onClose: () => void }) {
    const [draft, setDraft] = useState<Preferences>(preferences.value);
    const update = <K extends keyof Preferences>(key: K, value: Preferences[K]) => setDraft({ ...draft, [key]: value });
    return (
        <div {...stylex.props(styles.scrim)}>
            <Panel style={styles.dialog}>
                <Heading>Settings</Heading>
                <div {...stylex.props(styles.fade)}>
                    <span {...stylex.props(styles.fadeLabel)}>Chat fades after</span>
                    {fadeChoices.map((seconds) => (
                        <Button key={seconds} style={[styles.chip, draft.chatFadeSeconds === seconds && styles.chipActive]} onClick={() => update('chatFadeSeconds', seconds)}>
                            {seconds === 0 ? 'Never' : `${seconds}s`}
                        </Button>
                    ))}
                </div>
                <Stepper label="Chat text size" value={draft.chatScale} step={0.1} min={0.8} max={1.5} format={(value) => `${Math.round(value * 100)}%`} onChange={(value) => update('chatScale', value)} />
                <Stepper label="Interface scale" value={draft.uiScale} step={0.1} min={0.8} max={1.4} format={(value) => `${Math.round(value * 100)}%`} onChange={(value) => update('uiScale', value)} />
                <Toggle label="Chat timestamps" value={draft.chatTimestamps} onChange={(value) => update('chatTimestamps', value)} />
                <Toggle label="Film grain" value={draft.filmGrain} onChange={(value) => update('filmGrain', value)} />
                <Toggle label="Reduce motion" value={draft.reduceMotion} onChange={(value) => update('reduceMotion', value)} />
                <div {...stylex.props(styles.actions)}>
                    <Button onClick={props.onClose}>Cancel</Button>
                    <Button
                        variant="primary"
                        onClick={() => {
                            send('settings:save', draft);
                            props.onClose();
                        }}
                    >
                        Save
                    </Button>
                </div>
            </Panel>
        </div>
    );
}

export function QuitDialog(props: { onClose: () => void }) {
    return (
        <div {...stylex.props(styles.scrim)}>
            <Panel style={styles.dialog}>
                <Heading>Leave Lost Heaven?</Heading>
                <p {...stylex.props(styles.text)}>“Sooner or later, everybody has to pay their dues.”</p>
                <div {...stylex.props(styles.actions)}>
                    <Button onClick={props.onClose}>Stay</Button>
                    <Button variant="primary" onClick={() => send('app:quit')}>
                        Quit game
                    </Button>
                </div>
            </Panel>
        </div>
    );
}
