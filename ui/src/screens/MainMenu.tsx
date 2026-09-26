import * as stylex from '@stylexjs/stylex';
import { useEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Backdrop } from '../components/Backdrop';
import { Button, Divider, Field, Heading, Panel } from '../components/ui';
import { type ServerEntry, clampCodePoints, codePoints, preferences, state } from '../store';
import { colors, fonts, motion } from '../tokens.stylex';
import { QuitDialog, SettingsDialog } from './Dialogs';

const shimmer = stylex.keyframes({
    from: { backgroundPosition: '-12rem 0' },
    to: { backgroundPosition: '12rem 0' },
});

const titleIn = stylex.keyframes({
    from: { opacity: 0, letterSpacing: '0.5em', filter: 'blur(4px)' },
    to: { opacity: 1, letterSpacing: '0.14em', filter: 'blur(0)' },
});

const compact = '@media (max-height: 620px)';
const narrow = '@media (max-width: 560px)';

const styles = stylex.create({
    root: {
        position: 'absolute',
        inset: 0,
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr)',
        gridTemplateRows: 'auto minmax(0, 1fr) auto',
        rowGap: { default: '1.4rem', [compact]: '0.7rem' },
        padding: { default: 'max(5vh, 1.4rem) max(4.5vw, 1.4rem) max(4vh, 1.2rem)', [compact]: 'max(3.2vh, 0.9rem) max(3.5vw, 1rem) max(2.6vh, 0.8rem)' },
        color: colors.cream,
        fontFamily: fonts.body,
    },
    brand: {
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-start',
        gap: '0.15rem',
        justifySelf: 'start',
    },
    fan: {
        position: 'absolute',
        left: '-4rem',
        top: '-7.6rem',
        width: '24rem',
        height: '14rem',
        pointerEvents: 'none',
        opacity: 0.42,
    },
    title: {
        position: 'relative',
        margin: 0,
        fontFamily: fonts.display,
        fontWeight: 400,
        fontSize: { default: '4.4rem', [compact]: '3rem' },
        lineHeight: 0.95,
        letterSpacing: '0.14em',
        color: 'transparent',
        backgroundImage: 'linear-gradient(180deg, #fff6df 0%, #f2e6cb 38%, #d9b775 62%, #a47a3c 100%)',
        backgroundClip: 'text',
        filter: 'drop-shadow(0 0.22rem 0 rgba(77, 13, 15, 0.95)) drop-shadow(0 0.5rem 1.4rem rgba(0, 0, 0, 0.85))',
        animationName: titleIn,
        animationDuration: '1.2s',
        animationTimingFunction: motion.ease,
        animationFillMode: 'both',
    },
    subtitle: {
        position: 'relative',
        display: 'flex',
        alignItems: 'center',
        gap: '0.8rem',
        fontFamily: fonts.heading,
        fontSize: { default: '0.95rem', [compact]: '0.8rem' },
        fontWeight: 700,
        letterSpacing: '0.62em',
        textTransform: 'uppercase',
        color: colors.gold,
    },
    rule: {
        width: '3rem',
        height: '1px',
        backgroundImage: `linear-gradient(90deg, rgba(0, 0, 0, 0), ${colors.gold})`,
    },
    ruleRight: {
        backgroundImage: `linear-gradient(90deg, ${colors.gold}, rgba(0, 0, 0, 0))`,
    },
    tagline: {
        position: 'relative',
        marginTop: '0.3rem',
        fontStyle: 'italic',
        fontSize: '1rem',
        color: colors.muted,
        display: { default: 'block', [compact]: 'none' },
    },
    body: {
        display: 'flex',
        alignItems: 'center',
        gap: { default: '2.2rem', [compact]: '1.4rem' },
        minHeight: 0,
    },
    connect: {
        flexGrow: 0,
        flexShrink: 1,
        flexBasis: '26rem',
        minWidth: 0,
    },
    book: {
        flexGrow: 0,
        flexShrink: 1,
        flexBasis: '22rem',
        minWidth: '15rem',
        maxHeight: '100%',
        display: { default: 'flex', [narrow]: 'none' },
        flexDirection: 'column',
    },
    fields: {
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) minmax(4.5rem, 6rem)',
        gap: { default: '0.85rem 1rem', [compact]: '0.55rem 0.8rem' },
    },
    span: {
        gridColumn: '1 / -1',
    },
    status: {
        marginTop: { default: '1rem', [compact]: '0.7rem' },
        minHeight: { default: '2.4rem', [compact]: '1.6rem' },
        display: 'flex',
        flexDirection: 'column',
        gap: '0.45rem',
        fontFamily: fonts.body,
        fontStyle: 'italic',
        fontSize: '1rem',
        color: colors.parchment,
    },
    statusLine: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.55rem',
        minWidth: 0,
    },
    statusText: {
        minWidth: 0,
        overflowWrap: 'anywhere',
    },
    lamp: {
        width: '0.5rem',
        height: '0.5rem',
        flexShrink: 0,
        transform: 'rotate(45deg)',
        backgroundColor: colors.muted,
    },
    lampBusy: {
        backgroundColor: colors.goldBright,
        boxShadow: '0 0 0.6rem rgba(236, 208, 141, 0.8)',
    },
    lampGood: {
        backgroundColor: colors.success,
        boxShadow: '0 0 0.6rem rgba(143, 181, 114, 0.7)',
    },
    lampBad: {
        backgroundColor: colors.bloodBright,
    },
    error: {
        color: '#f0b5a8',
    },
    progress: {
        position: 'relative',
        height: '3px',
        backgroundColor: 'rgba(201, 164, 92, 0.15)',
        overflow: 'hidden',
    },
    progressFill: {
        position: 'absolute',
        top: 0,
        bottom: 0,
        left: 0,
        backgroundColor: colors.gold,
        transitionProperty: 'width',
        transitionDuration: motion.medium,
    },
    progressSweep: {
        position: 'absolute',
        inset: 0,
        backgroundImage: 'linear-gradient(90deg, rgba(0, 0, 0, 0), rgba(236, 208, 141, 0.9), rgba(0, 0, 0, 0))',
        backgroundSize: '12rem 100%',
        backgroundRepeat: 'no-repeat',
        animationName: shimmer,
        animationDuration: '1.4s',
        animationIterationCount: 'infinite',
        animationTimingFunction: 'linear',
    },
    actions: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.55rem',
        marginTop: { default: '0.9rem', [compact]: '0.6rem' },
    },
    downloadDetails: {
        overflowWrap: 'anywhere',
        color: colors.parchment,
        fontSize: '0.85rem',
    },
    favorite: {
        marginTop: { default: '0.6rem', [compact]: '0.3rem' },
        display: 'flex',
        justifyContent: 'center',
    },
    ticket: {
        position: 'relative',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: '0.3rem',
        padding: { default: '0.9rem 1rem', [compact]: '0.6rem 0.8rem' },
        borderWidth: '1px',
        borderStyle: 'dashed',
        borderColor: colors.goldLine,
        backgroundColor: 'rgba(0, 0, 0, 0.3)',
        backgroundImage: 'radial-gradient(120% 140% at 50% 0%, rgba(139, 26, 26, 0.22), rgba(0, 0, 0, 0) 70%)',
    },
    ticketLabel: {
        fontFamily: fonts.heading,
        fontSize: '0.64rem',
        fontWeight: 700,
        letterSpacing: '0.3em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    ticketAddress: {
        maxWidth: '100%',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
        fontFamily: fonts.heading,
        fontStyle: 'italic',
        fontSize: '1.3rem',
        color: colors.goldBright,
    },
    tabs: {
        display: 'flex',
        gap: '0.4rem',
        marginBottom: '0.2rem',
    },
    tab: {
        flexGrow: 1,
        flexBasis: 0,
        minWidth: 0,
        minHeight: '2rem',
        padding: '0 0.6rem',
        fontSize: '0.7rem',
    },
    tabActive: {
        borderColor: colors.gold,
        color: colors.goldBright,
        backgroundColor: 'rgba(77, 13, 15, 0.55)',
    },
    list: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.35rem',
        overflowY: 'auto',
        minHeight: '5rem',
        maxHeight: '19rem',
        paddingRight: '0.2rem',
    },
    empty: {
        padding: '1.4rem 0.4rem',
        textAlign: 'center',
        fontStyle: 'italic',
        color: colors.muted,
    },
    entry: {
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) auto',
        alignItems: 'center',
        gap: '0.4rem',
        padding: '0.45rem 0.5rem 0.45rem 0.7rem',
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: 'transparent',
        borderLeftColor: colors.goldHair,
        backgroundColor: 'rgba(0, 0, 0, 0.28)',
        color: colors.cream,
        transitionProperty: 'border-color, background-color',
        transitionDuration: motion.quick,
        ':hover': {
            borderColor: colors.goldHair,
            borderLeftColor: colors.gold,
            backgroundColor: 'rgba(58, 40, 25, 0.55)',
        },
    },
    entryButton: {
        appearance: 'none',
        minWidth: 0,
        padding: 0,
        borderWidth: 0,
        backgroundColor: 'transparent',
        color: 'inherit',
        textAlign: 'left',
        outline: 'none',
    },
    entryName: {
        display: 'block',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        textOverflow: 'ellipsis',
        fontFamily: fonts.heading,
        fontSize: '0.95rem',
    },
    entryMeta: {
        display: 'block',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        textOverflow: 'ellipsis',
        fontFamily: fonts.body,
        fontStyle: 'italic',
        fontSize: '0.82rem',
        color: colors.muted,
    },
    entryActions: {
        display: 'flex',
        gap: '0.1rem',
    },
    icon: {
        minHeight: '1.8rem',
        minWidth: '1.8rem',
        padding: 0,
        fontSize: '0.9rem',
        letterSpacing: 0,
    },
    starOn: {
        color: colors.goldBright,
    },
    footer: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.6rem',
        minWidth: 0,
    },
    version: {
        marginLeft: 'auto',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        textOverflow: 'ellipsis',
        fontFamily: fonts.heading,
        fontStyle: 'italic',
        fontSize: '0.8rem',
        letterSpacing: '0.06em',
        color: 'rgba(168, 146, 109, 0.75)',
    },
});

function Fan() {
    const rays = Array.from({ length: 13 }, (_, index) => -90 + index * 15);
    return (
        <svg {...stylex.props(styles.fan)} viewBox="0 0 220 140" aria-hidden="true">
            <defs>
                <radialGradient id="fan" cx="50%" cy="100%" r="100%">
                    <stop offset="0%" stop-color="#c9a45c" stop-opacity="0.55" />
                    <stop offset="100%" stop-color="#c9a45c" stop-opacity="0" />
                </radialGradient>
            </defs>
            {rays.map((angle) => (
                <path key={angle} d="M110 140 L106 10 L114 10 Z" fill="url(#fan)" transform={`rotate(${angle / 1.6} 110 140)`} />
            ))}
            <path d="M40 140 A70 70 0 0 1 180 140" fill="none" stroke="#c9a45c" stroke-opacity="0.35" stroke-width="0.8" />
            <path d="M62 140 A48 48 0 0 1 158 140" fill="none" stroke="#c9a45c" stroke-opacity="0.25" stroke-width="0.6" />
        </svg>
    );
}

function sameServer(a: { host: string; port: number }, b: { host: string; port: number }) {
    return a.host === b.host && a.port === b.port;
}

function ago(seconds: number): string {
    if (!seconds) {
        return '';
    }
    const delta = Math.max(0, Date.now() / 1000 - seconds);
    if (delta < 90) {
        return 'just now';
    }
    if (delta < 3600) {
        return `${Math.round(delta / 60)} min ago`;
    }
    if (delta < 86400 * 2) {
        return `${Math.round(delta / 3600)} h ago`;
    }
    return `${Math.round(delta / 86400)} days ago`;
}

export function MainMenu() {
    const current = state.value;
    const { connection, defaults, settings, limits } = current;
    const [nickname, setNickname] = useState(defaults.nickname);
    const [host, setHost] = useState(defaults.host);
    const [port, setPort] = useState(String(defaults.port));
    const [password, setPassword] = useState('');
    const [touched, setTouched] = useState(false);
    const [tab, setTab] = useState<'recent' | 'favorites'>(settings.favorites.length > 0 && settings.recent.length === 0 ? 'favorites' : 'recent');
    const [dialog, setDialog] = useState<'settings' | 'quit' | null>(null);
    const nicknameRef = useRef<HTMLInputElement>(null);

    // Pick up defaults the client learns later (quick-join config, saved nickname).
    const seeded = useRef(false);
    useEffect(() => {
        if (seeded.current || !current.version) {
            return;
        }
        seeded.current = true;
        setNickname(defaults.nickname);
        setHost(defaults.host);
        setPort(String(defaults.port));
    }, [current.version, defaults.nickname, defaults.host, defaults.port]);

    useEffect(() => {
        const timer = window.setTimeout(() => nicknameRef.current?.focus(), 120);
        return () => window.clearTimeout(timer);
    }, []);

    useEffect(() => {
        const onKey = (event: KeyboardEvent) => {
            if (event.key === 'Escape' && dialog) {
                event.preventDefault();
                setDialog(null);
            }
        };
        window.addEventListener('keydown', onKey);
        return () => window.removeEventListener('keydown', onKey);
    }, [dialog]);

    const portNumber = Number.parseInt(port, 10);
    const nicknameValid = codePoints(nickname.trim()) > 0;
    const hostValid = /^[\x21-\x7e]{1,255}$/.test(host.trim());
    const portValid = /^\d{1,5}$/.test(port) && portNumber > 0 && portNumber <= 65535;
    const busy = connection.active && !connection.playAvailable;
    const preparingContent = connection.phase === 'downloading' || connection.phase === 'starting';
    const downloadPercent = Math.round(Math.max(0, Math.min(1, connection.progress)) * 100);
    const measuredDownload = connection.downloading && connection.bytesTotal > 0;
    const mb = (bytes: number) => (bytes / (1024 * 1024)).toFixed(1);
    const disconnected = !connection.active;
    const isError = disconnected && /^(Disconnected:|Could not|Failed|Enter|Choose|The password|Disconnect before|Nickname|Password)/.test(connection.status);
    const target = { host: host.trim(), port: portNumber };
    const isFavorite = settings.favorites.some((entry) => sameServer(entry, target));

    const connect = (override?: ServerEntry) => {
        setTouched(true);
        const request = override ? { host: override.host, port: override.port } : target;
        if (!nicknameValid || (!override && (!hostValid || !portValid))) {
            return;
        }
        if (override) {
            setHost(override.host);
            setPort(String(override.port));
        }
        send('menu:connect', { nickname: nickname.trim(), host: request.host, port: request.port, password });
    };

    const fill = (entry: ServerEntry) => {
        setHost(entry.host);
        setPort(String(entry.port));
    };

    const lamp = connection.playAvailable ? styles.lampGood : busy ? styles.lampBusy : isError ? styles.lampBad : null;
    const list = tab === 'recent' ? settings.recent : settings.favorites;
    const reduceMotion = preferences.value.reduceMotion;

    return (
        <>
            <Backdrop />
            <div {...stylex.props(styles.root)}>
                <header {...stylex.props(styles.brand)}>
                    <Fan />
                    <h1 {...stylex.props(styles.title)}>MAFIA</h1>
                    <div {...stylex.props(styles.subtitle)}>
                        <span {...stylex.props(styles.rule)} />
                        Online
                        <span {...stylex.props(styles.rule, styles.ruleRight)} />
                    </div>
                    <div {...stylex.props(styles.tagline)}>The City of Lost Heaven, 1930</div>
                </header>

                <main {...stylex.props(styles.body)}>
                    <Panel style={styles.connect}>
                        <Heading>{connection.playAvailable ? 'The family awaits' : 'Make the connection'}</Heading>
                        {disconnected ? (
                            <div {...stylex.props(styles.fields)}>
                                <Field
                                    style={styles.span}
                                    label="Nickname"
                                    hint={`${codePoints(nickname)}/${limits.nickname}`}
                                    value={nickname}
                                    inputRef={nicknameRef}
                                    invalid={touched && !nicknameValid}
                                    placeholder="Tommy Angelo"
                                    onInput={(value) => setNickname(clampCodePoints(value, limits.nickname))}
                                    onEnter={() => connect()}
                                />
                                <Field label="Server address" value={host} invalid={touched && !hostValid} placeholder="127.0.0.1" onInput={(value) => setHost(value.replace(/\s/g, ''))} onEnter={() => connect()} />
                                <Field label="Port" value={port} inputMode="numeric" maxLength={5} invalid={touched && !portValid} onInput={(value) => setPort(value.replace(/\D/g, ''))} onEnter={() => connect()} />
                                <Field style={styles.span} label="Password" hint="if the server asks" type="password" value={password} maxLength={128} onInput={setPassword} onEnter={() => connect()} />
                            </div>
                        ) : (
                            <div {...stylex.props(styles.ticket)}>
                                <span {...stylex.props(styles.ticketLabel)}>{connection.playAvailable ? 'Your table is ready at' : 'Calling on'}</span>
                                <span {...stylex.props(styles.ticketAddress)}>
                                    {connection.host}:{connection.port}
                                </span>
                            </div>
                        )}

                        <div {...stylex.props(styles.status)} role="status">
                            <div {...stylex.props(styles.statusLine, isError && styles.error)}>
                                <span {...stylex.props(styles.lamp, lamp)} />
                                <span {...stylex.props(styles.statusText)}>{connection.status}</span>
                            </div>
                            {busy && (
                                <div {...stylex.props(styles.progress)} role="progressbar" aria-label={preparingContent ? 'Server content download' : 'Connecting to server'} aria-valuemin={0} aria-valuemax={100} aria-valuenow={measuredDownload ? downloadPercent : undefined}>
                                    {measuredDownload ? (
                                        <div {...stylex.props(styles.progressFill)} style={{ width: `${downloadPercent}%` }} />
                                    ) : (
                                        !reduceMotion && <div {...stylex.props(styles.progressSweep)} />
                                    )}
                                </div>
                            )}
                            {busy && preparingContent && (
                                <div {...stylex.props(styles.downloadDetails)}>
                                    {connection.filesTotal > 0 && <div>{connection.filesDownloaded} / {connection.filesTotal} files · {mb(connection.bytesDownloaded)} / {mb(connection.bytesTotal)} MB · {downloadPercent}%</div>}
                                    {connection.currentFile && <div>{connection.currentFile}</div>}
                                </div>
                            )}
                        </div>

                        <div {...stylex.props(styles.actions)}>
                            {disconnected && (
                                <Button variant="primary" size="large" wide onClick={() => connect()}>
                                    Connect
                                </Button>
                            )}
                            {connection.playAvailable && (
                                <Button variant="primary" size="large" wide onClick={() => send('menu:play')}>
                                    Enter Lost Heaven
                                </Button>
                            )}
                            {connection.active && (
                                <Button variant={connection.playAvailable ? 'ghost' : undefined} wide onClick={() => send('menu:disconnect')}>
                                    {busy ? 'Cancel' : 'Disconnect'}
                                </Button>
                            )}
                        </div>
                        {disconnected && hostValid && portValid && (
                            <div {...stylex.props(styles.favorite)}>
                                <Button variant="ghost" onClick={() => send('servers:favorite', { host: target.host, port: target.port, name: '', favorite: !isFavorite })}>
                                    <span {...stylex.props(isFavorite && styles.starOn)}>{isFavorite ? '★' : '☆'}</span>
                                    {isFavorite ? 'In favorites' : 'Add to favorites'}
                                </Button>
                            </div>
                        )}
                    </Panel>

                    <Panel style={styles.book} delay={120}>
                        <div {...stylex.props(styles.tabs)}>
                            <Button style={[styles.tab, tab === 'recent' && styles.tabActive]} onClick={() => setTab('recent')}>
                                Recent
                            </Button>
                            <Button style={[styles.tab, tab === 'favorites' && styles.tabActive]} onClick={() => setTab('favorites')}>
                                Favorites
                            </Button>
                        </div>
                        <Divider />
                        <div {...stylex.props(styles.list)}>
                            {list.length === 0 && <div {...stylex.props(styles.empty)}>{tab === 'recent' ? 'No joints visited yet.' : 'Star a server to keep its address here.'}</div>}
                            {list.map((entry) => {
                                const favorite = settings.favorites.some((item) => sameServer(item, entry));
                                return (
                                    <div key={`${tab}:${entry.host}:${entry.port}`} {...stylex.props(styles.entry)}>
                                        <button type="button" {...stylex.props(styles.entryButton)} onClick={() => fill(entry)}>
                                            <span>
                                                <span {...stylex.props(styles.entryName)}>{entry.name || entry.host}</span>
                                                <span {...stylex.props(styles.entryMeta)}>
                                                    {entry.host}:{entry.port}
                                                    {entry.lastUsed ? ` · ${ago(entry.lastUsed)}` : ''}
                                                </span>
                                            </span>
                                        </button>
                                        <span {...stylex.props(styles.entryActions)}>
                                            <Button
                                                variant="ghost"
                                                style={[styles.icon, favorite && styles.starOn]}
                                                title={favorite ? 'Remove from favorites' : 'Add to favorites'}
                                                onClick={() => send('servers:favorite', { host: entry.host, port: entry.port, name: entry.name, favorite: !favorite })}
                                            >
                                                {favorite ? '★' : '☆'}
                                            </Button>
                                            {tab === 'recent' && (
                                                <Button variant="ghost" style={styles.icon} title="Forget" onClick={() => send('servers:forget', { host: entry.host, port: entry.port })}>
                                                    ✕
                                                </Button>
                                            )}
                                            <Button variant="ghost" style={styles.icon} title="Connect" disabled={!disconnected} onClick={() => connect(entry)}>
                                                ➜
                                            </Button>
                                        </span>
                                    </div>
                                );
                            })}
                        </div>
                    </Panel>
                </main>

                <footer {...stylex.props(styles.footer)}>
                    <Button onClick={() => setDialog('settings')}>Settings</Button>
                    <Button onClick={() => setDialog('quit')}>Quit</Button>
                    <span {...stylex.props(styles.version)}>Mafia1Online {current.version}</span>
                </footer>
            </div>
            {dialog === 'settings' && <SettingsDialog onClose={() => setDialog(null)} />}
            {dialog === 'quit' && <QuitDialog onClose={() => setDialog(null)} />}
        </>
    );
}
