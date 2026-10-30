import * as stylex from '@stylexjs/stylex';
import { useEffect, useRef, useState } from 'preact/hooks';
import { send } from '../bridge';
import { Backdrop } from '../components/Backdrop';
import { Button, Divider, Field, Heading, Panel } from '../components/ui';
import { type ServerEntry, clampCodePoints, codePoints, preferences, state } from '../store';
import { colors, fonts, radii } from '../tokens.stylex';
import { QuitDialog, SettingsDialog } from './Dialogs';

const compact = '@media (max-height: 620px)';
const narrow = '@media (max-width: 560px)';

// The selection marker shared with the rest of the UI: a red bar on the left.
const marker = `inset 3px 0 0 ${colors.accent}`;

const styles = stylex.create({
    root: {
        position: 'absolute',
        inset: 0,
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr)',
        gridTemplateRows: 'auto minmax(0, 1fr) auto',
        rowGap: { default: '1.5rem', [compact]: '0.75rem' },
        padding: { default: 'max(5vh, 1.5rem) max(4.5vw, 1.5rem) max(4vh, 1.25rem)', [compact]: 'max(3.2vh, 1rem) max(3.5vw, 1rem) max(2.6vh, 0.75rem)' },
        color: colors.text,
    },
    brand: {
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'flex-start',
        gap: '0.5rem',
        justifySelf: 'start',
    },
    title: {
        margin: 0,
        fontFamily: fonts.display,
        fontWeight: 700,
        fontSize: { default: '4.5rem', [compact]: '3.25rem' },
        lineHeight: 0.9,
        letterSpacing: '0.03em',
        color: colors.text,
    },
    // "Online" sits on a hairline the width of the title, like a title card.
    subtitle: {
        alignSelf: 'stretch',
        paddingTop: '0.5rem',
        borderTopWidth: '1px',
        borderTopStyle: 'solid',
        borderTopColor: colors.line,
        fontSize: { default: '0.9rem', [compact]: '0.75rem' },
        fontWeight: 500,
        letterSpacing: '0.62em',
        textTransform: 'uppercase',
        color: colors.text,
    },
    tagline: {
        fontSize: '0.85rem',
        color: colors.muted,
        display: { default: 'block', [compact]: 'none' },
    },
    body: {
        display: 'flex',
        alignItems: 'center',
        gap: { default: '2rem', [compact]: '1.5rem' },
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
        gap: { default: '0.75rem 1rem', [compact]: '0.5rem 0.75rem' },
    },
    span: {
        gridColumn: '1 / -1',
    },
    status: {
        marginTop: { default: '1rem', [compact]: '0.75rem' },
        minHeight: { default: '2.25rem', [compact]: '1.5rem' },
        display: 'flex',
        flexDirection: 'column',
        gap: '0.5rem',
        fontSize: '0.875rem',
        color: colors.muted,
    },
    statusLine: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        minWidth: 0,
    },
    statusText: {
        minWidth: 0,
        overflowWrap: 'anywhere',
    },
    lamp: {
        width: '0.45rem',
        height: '0.45rem',
        flexShrink: 0,
        backgroundColor: colors.faint,
    },
    lampBusy: {
        backgroundColor: colors.text,
    },
    lampGood: {
        backgroundColor: colors.success,
    },
    lampBad: {
        backgroundColor: colors.danger,
    },
    error: {
        color: colors.danger,
    },
    progress: {
        position: 'relative',
        height: '3px',
        backgroundColor: colors.raised,
        overflow: 'hidden',
    },
    progressFill: {
        position: 'absolute',
        top: 0,
        bottom: 0,
        left: 0,
        backgroundColor: colors.accentHover,
    },
    // Unmeasured progress is a static tint, not a looping sweep: CEF would
    // repaint the bar every frame for as long as the connection takes.
    progressSweep: {
        position: 'absolute',
        inset: 0,
        backgroundColor: colors.accent,
        opacity: 0.5,
    },
    actions: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.5rem',
        marginTop: { default: '1rem', [compact]: '0.75rem' },
    },
    downloadDetails: {
        overflowWrap: 'anywhere',
        color: colors.muted,
        fontSize: '0.8rem',
        fontVariantNumeric: 'tabular-nums',
    },
    favorite: {
        marginTop: { default: '0.5rem', [compact]: '0.25rem' },
        display: 'flex',
        justifyContent: 'center',
    },
    ticket: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.25rem',
        padding: { default: '1rem', [compact]: '0.75rem' },
        borderWidth: '1px',
        borderStyle: 'solid',
        borderColor: colors.border,
        borderRadius: radii.sm,
        backgroundColor: colors.bg,
    },
    ticketLabel: {
        fontSize: '0.65rem',
        fontWeight: 600,
        letterSpacing: '0.16em',
        textTransform: 'uppercase',
        color: colors.muted,
    },
    ticketAddress: {
        maxWidth: '100%',
        overflow: 'hidden',
        textOverflow: 'ellipsis',
        whiteSpace: 'nowrap',
        fontSize: '1.25rem',
        fontWeight: 600,
        color: colors.text,
    },
    tabs: {
        display: 'flex',
        gap: '0.5rem',
    },
    tab: {
        flexGrow: 1,
        flexBasis: 0,
        minWidth: 0,
        minHeight: '2rem',
        padding: '0 0.75rem',
        fontSize: '0.7rem',
    },
    tabActive: {
        borderColor: colors.text,
        backgroundColor: colors.raised,
        boxShadow: marker,
    },
    list: {
        display: 'flex',
        flexDirection: 'column',
        gap: '0.25rem',
        overflowY: 'auto',
        minHeight: '5rem',
        maxHeight: '19rem',
        paddingRight: '0.25rem',
    },
    empty: {
        padding: '1.5rem 0.5rem',
        textAlign: 'center',
        fontSize: '0.875rem',
        color: colors.muted,
    },
    entry: {
        display: 'grid',
        gridTemplateColumns: 'minmax(0, 1fr) auto',
        alignItems: 'center',
        gap: '0.5rem',
        padding: '0.5rem 0.5rem 0.5rem 0.75rem',
        borderWidth: '0 0 1px 0',
        borderStyle: 'solid',
        borderColor: colors.border,
        color: colors.text,
        ':hover': {
            backgroundColor: colors.raised,
            boxShadow: marker,
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
        fontSize: '0.92rem',
        fontWeight: 500,
    },
    entryMeta: {
        display: 'block',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        textOverflow: 'ellipsis',
        fontSize: '0.78rem',
        color: colors.muted,
    },
    entryActions: {
        display: 'flex',
        gap: '0.125rem',
    },
    icon: {
        minHeight: '1.75rem',
        minWidth: '1.75rem',
        padding: 0,
        fontSize: '0.9rem',
        letterSpacing: 0,
    },
    starOn: {
        color: colors.accentHover,
    },
    footer: {
        display: 'flex',
        alignItems: 'center',
        gap: '0.5rem',
        minWidth: 0,
    },
    version: {
        marginLeft: 'auto',
        overflow: 'hidden',
        whiteSpace: 'nowrap',
        textOverflow: 'ellipsis',
        fontSize: '0.68rem',
        letterSpacing: '0.14em',
        textTransform: 'uppercase',
        color: colors.faint,
    },
});

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
                    <h1 {...stylex.props(styles.title)}>MAFIA</h1>
                    <div {...stylex.props(styles.subtitle)}>Online</div>
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
