import * as stylex from '@stylexjs/stylex';
import { send } from '../bridge';
import { Button, Heading, Panel } from '../components/ui';
import { SettingsScreen } from '../settings/SettingsScreen';
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
});

// The full settings screen over the main menu.
export function SettingsDialog(props: { onClose: () => void }) {
    return (
        <div {...stylex.props(styles.scrim)}>
            <SettingsScreen onClose={props.onClose} />
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
