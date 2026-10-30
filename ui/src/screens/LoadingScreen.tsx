import * as stylex from '@stylexjs/stylex';
import { useEffect, useState } from 'preact/hooks';
import { type LoadingState, loading, state } from '../store';
import { colors, fonts, motion } from '../tokens.stylex';

const compact = '@media (max-height: 620px)';

// Shown instead of the retail loading screen while a server mission loads.
// The game does nothing else meanwhile: the client pumps this page only from
// the loader's own frames (every couple of percent), so the screen is static
// apart from the bar and a line that changes every few seconds.
const styles = stylex.create({
    // Opaque: the client presents a cleared frame beneath it.
    root: {
        position: 'absolute',
        inset: 0,
        overflow: 'hidden',
        backgroundColor: colors.bg,
        color: colors.text,
    },
    // Light through a window on the right, crossed by venetian blinds; the
    // left, where the type sits, stays in shadow. Static.
    light: {
        position: 'absolute',
        inset: 0,
        backgroundImage:
            'linear-gradient(104deg, rgba(11, 11, 12, 1) 0%, rgba(11, 11, 12, 1) 46%, rgba(11, 11, 12, 0.5) 56%, rgba(11, 11, 12, 0.15) 100%), repeating-linear-gradient(166deg, rgba(0, 0, 0, 0.66) 0 1.5rem, rgba(0, 0, 0, 0) 1.5rem 2.6rem), linear-gradient(180deg, #514a3e 0%, #39342c 55%, #1b1916 100%)',
    },
    layout: {
        position: 'absolute',
        inset: 0,
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr)',
        gridTemplateRows: 'auto minmax(0, 1fr) auto auto',
        rowGap: { default: '2rem', [compact]: '1rem' },
        padding: { default: 'max(5vh, 1.5rem) max(4.5vw, 1.5rem) max(4vh, 1.25rem)', [compact]: 'max(3.2vh, 1rem) max(3.5vw, 1rem) max(2.6vh, 0.75rem)' },
    },
    brand: {
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-start',
        gap: '0.5rem',
        justifySelf: 'start',
    },
    brandTitle: {
        margin: 0,
        fontFamily: fonts.display,
        fontWeight: 700,
        fontSize: { default: '3rem', [compact]: '2.25rem' },
        lineHeight: 0.9,
        letterSpacing: '0.03em',
    },
    brandSubtitle: {
        alignSelf: 'stretch',
        paddingTop: '0.4rem',
        borderTopWidth: '1px',
        borderTopStyle: 'solid',
        borderTopColor: colors.line,
        fontSize: { default: '0.7rem', [compact]: '0.6rem' },
        fontWeight: 500,
        letterSpacing: '0.62em',
        textTransform: 'uppercase',
    },
    destination: {
        alignSelf: 'end',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-start',
        minWidth: 0,
        maxWidth: '40rem',
    },
    label: {
        fontSize: '0.68rem',
        fontWeight: 600,
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    mission: {
        margin: '0.5rem 0 0',
        maxWidth: '100%',
        fontFamily: fonts.display,
        fontWeight: 700,
        fontSize: { default: '3.75rem', [compact]: '2.75rem' },
        lineHeight: 1,
        overflowWrap: 'anywhere',
    },
    missionDetail: {
        marginTop: '0.5rem',
        fontSize: '0.85rem',
        letterSpacing: '0.08em',
        color: colors.muted,
    },
    // The double hairline titles sit on, as wide as the block below.
    rule: {
        alignSelf: 'stretch',
        height: 0,
        margin: { default: '1.25rem 0 1rem', [compact]: '0.75rem 0 0.75rem' },
        borderTopWidth: '3px',
        borderTopStyle: 'double',
        borderTopColor: colors.line,
    },
    facts: {
        display: 'grid',
        gridTemplateColumns: 'auto minmax(0, 1fr)',
        columnGap: '1.5rem',
        rowGap: '0.5rem',
        alignItems: 'baseline',
        minWidth: 0,
        maxWidth: '100%',
    },
    factValue: {
        minWidth: 0,
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
        fontSize: '1rem',
        fontWeight: 600,
    },
    factMuted: {
        marginLeft: '0.6rem',
        fontWeight: 400,
        color: colors.muted,
    },
    word: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.5rem',
        maxWidth: '34rem',
        minHeight: { default: '4.5rem', [compact]: '3.75rem' },
        paddingLeft: '1rem',
        borderLeftWidth: '3px',
        borderLeftStyle: 'solid',
        borderLeftColor: colors.accent,
        transitionProperty: 'opacity',
        transitionDuration: '320ms',
        transitionTimingFunction: motion.ease,
    },
    wordHidden: {
        opacity: 0,
    },
    wordText: {
        margin: 0,
        fontSize: { default: '1.05rem', [compact]: '0.95rem' },
        lineHeight: 1.45,
        color: colors.text,
    },
    footer: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.6rem',
    },
    status: {
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'baseline',
        gap: '1rem',
    },
    stage: {
        fontSize: '0.75rem',
        fontWeight: 600,
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
    },
    percent: {
        fontSize: '0.75rem',
        fontWeight: 600,
        letterSpacing: '0.12em',
        fontVariantNumeric: 'tabular-nums',
        color: colors.muted,
    },
    bar: {
        position: 'relative',
        height: '3px',
        backgroundColor: colors.raised,
        overflow: 'hidden',
    },
    fill: {
        position: 'absolute',
        top: 0,
        bottom: 0,
        left: 0,
        backgroundColor: colors.accentHover,
        transitionProperty: 'width',
        transitionDuration: motion.medium,
        transitionTimingFunction: motion.ease,
    },
    // Before the loader reports anything: a static tint, never a sweep.
    pending: {
        position: 'absolute',
        inset: 0,
        backgroundColor: colors.accent,
        opacity: 0.35,
    },
    version: {
        alignSelf: 'flex-end',
        fontSize: '0.6rem',
        fontWeight: 600,
        letterSpacing: '0.2em',
        textTransform: 'uppercase',
        color: colors.faint,
    },
});

// The free ride scenes by name; any other mission gets its directory name
// tidied up (mise06-autodrom -> Mission 6 · Autodrom).
const kMissionNames: Record<string, { title: string; detail: string }> = {
    freeride: { title: 'Lost Heaven', detail: 'Free ride' },
    freeridenoc: { title: 'Lost Heaven by Night', detail: 'Free ride' },
    freekrajina: { title: 'The Countryside', detail: 'Free ride' },
    freekrajinanoc: { title: 'The Countryside by Night', detail: 'Free ride' },
};

function capitalize(word: string): string {
    return word ? word[0].toUpperCase() + word.slice(1) : word;
}

export function missionName(id: string): { title: string; detail: string } {
    const known = kMissionNames[id.toLowerCase()];
    if (known) {
        return known;
    }
    const story = /^mise(\d+)([a-z]?)(?:-(.+))?$/i.exec(id);
    if (story) {
        const part = story[3] ? story[3].split(/[\s_-]+/).map(capitalize).join(' ') : '';
        return { title: part || `Mission ${Number(story[1])}`, detail: `Mission ${Number(story[1])}${story[2].toLowerCase()}` };
    }
    const words = id.split(/[\s_-]+/).filter(Boolean).map(capitalize).join(' ');
    return { title: words || 'Lost Heaven', detail: id ? id : 'Server mission' };
}

const kStages: Record<LoadingState['stage'], string> = {
    preparing: 'Hailing a cab',
    scene: 'Raising the city',
    world: 'Waking the streets',
};

interface Word {
    label: string;
    text: string;
}

// Useful first, then the city. Keys are the defaults; Settings can move them.
const kWords: Word[] = [
    { label: 'Tip', text: 'Press T to talk and / to give the server a command. Hold F1 to see who else is in town.' },
    { label: 'Tip', text: 'Esc opens the pause screen, with the settings and the way out.' },
    { label: 'Tip', text: 'Every key can be moved under Settings, the multiplayer ones included.' },
    { label: 'Tip', text: 'Voice chat carries about as far as a raised voice. Pick push-to-talk or voice activity under Settings, Voice.' },
    { label: 'Word on the street', text: 'Lost Heaven, 1930. Ten years of Prohibition, and the city has never been thirstier.' },
    { label: 'Word on the street', text: 'Little Italy, Hoboken, Chinatown, the Works Quarter: every corner of this town answers to somebody.' },
    { label: 'Word on the street', text: 'A man on foot in Lost Heaven is a man waiting for a ride. Make sure it is the right one.' },
    { label: 'Word on the street', text: 'Keep your friends close and your engine running.' },
];

const kWordSeconds = 9;
const kWordFadeMs = 320;

function useRotatingWord(): { word: Word; shown: boolean } {
    const [index, setIndex] = useState(() => Math.floor(Math.random() * kWords.length));
    const [shown, setShown] = useState(true);
    useEffect(() => {
        let swap = 0;
        const timer = window.setInterval(() => {
            setShown(false);
            swap = window.setTimeout(() => {
                setIndex((current) => (current + 1) % kWords.length);
                setShown(true);
            }, kWordFadeMs);
        }, kWordSeconds * 1000);
        return () => {
            window.clearInterval(timer);
            window.clearTimeout(swap);
        };
    }, []);
    return { word: kWords[index], shown };
}

export function LoadingScreen() {
    const report = loading.value;
    const { connection, settings, version } = state.value;
    const { word, shown } = useRotatingWord();

    const mission = missionName(report.mission);
    const address = connection.host ? `${connection.host}:${connection.port}` : '';
    const book = [...settings.favorites, ...settings.recent].find((entry) => entry.host === connection.host && entry.port === connection.port && entry.name);
    const measured = report.progress >= 0;
    const percent = Math.round(Math.min(Math.max(report.progress, 0), 1) * 100);

    return (
        <div {...stylex.props(styles.root)} role="status" aria-live="polite">
            <div {...stylex.props(styles.light)} aria-hidden="true" />
            <div {...stylex.props(styles.layout)}>
                <header {...stylex.props(styles.brand)}>
                    <h1 {...stylex.props(styles.brandTitle)}>MAFIA</h1>
                    <div {...stylex.props(styles.brandSubtitle)}>Online</div>
                </header>

                <section {...stylex.props(styles.destination)}>
                    <div {...stylex.props(styles.label)}>Tonight in</div>
                    <h2 {...stylex.props(styles.mission)}>{mission.title}</h2>
                    <div {...stylex.props(styles.missionDetail)}>{mission.detail}</div>
                    <div {...stylex.props(styles.rule)} />
                    <div {...stylex.props(styles.facts)}>
                        {address && (
                            <>
                                <span {...stylex.props(styles.label)}>Server</span>
                                <span {...stylex.props(styles.factValue)}>
                                    {book ? book.name : address}
                                    {book && <span {...stylex.props(styles.factMuted)}>{address}</span>}
                                </span>
                            </>
                        )}
                        {settings.nickname && (
                            <>
                                <span {...stylex.props(styles.label)}>Playing as</span>
                                <span {...stylex.props(styles.factValue)}>{settings.nickname}</span>
                            </>
                        )}
                    </div>
                </section>

                <aside {...stylex.props(styles.word, !shown && styles.wordHidden)}>
                    <div {...stylex.props(styles.label)}>{word.label}</div>
                    <p {...stylex.props(styles.wordText)}>{word.text}</p>
                </aside>

                <footer {...stylex.props(styles.footer)}>
                    <div {...stylex.props(styles.status)}>
                        <span {...stylex.props(styles.stage)}>{measured && percent >= 100 ? 'Almost there' : kStages[report.stage]}</span>
                        <span {...stylex.props(styles.percent)}>{measured ? `${percent}%` : ''}</span>
                    </div>
                    <div {...stylex.props(styles.bar)}>
                        {measured ? <div {...stylex.props(styles.fill)} style={{ width: `${percent}%` }} /> : <div {...stylex.props(styles.pending)} />}
                    </div>
                    {version && <div {...stylex.props(styles.version)}>Mafia1Online {version}</div>}
                </footer>
            </div>
        </div>
    );
}
