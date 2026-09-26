import * as stylex from '@stylexjs/stylex';
import { useEffect, useRef, useState } from 'preact/hooks';
import {
    credentialsError,
    EVENT,
    isLocale,
    isRememberedAccount,
    type AuthMode,
    type Locale,
    type ViewState,
    type RememberedAccount,
} from '../shared/protocol';
import en from '../shared/locales/en.json';
import cs from '../shared/locales/cs.json';
import { Arrow, Button, Field, Panel, Seal } from './components';
import { inGame, send, subscribe } from './bridge';
import { theme } from './tokens.stylex';
import { Recorder, useRecorder } from './Recorder';
import {
    forgetRememberedAccount,
    loadRememberedAccount,
    saveRememberedAccount,
} from './remembered-account';

const dictionaries: Record<Locale, typeof en> = { en, cs };

function initialLocale(): Locale {
    try {
        const stored = localStorage.getItem('lhrp.locale');

        if (isLocale(stored)) {
            return stored;
        }
    } catch {
        /* CEF storage may be disabled by the host. */
    }

    return navigator.language.startsWith('cs') ? 'cs' : 'en';
}

export function App() {
    const [locale, setLocale] = useState(initialLocale);
    const [mode, setMode] = useState<AuthMode>('login');
    const [username, setUsername] = useState('');
    const [password, setPassword] = useState('');
    const [confirm, setConfirm] = useState('');
    const [reveal, setReveal] = useState(false);
    const [remember, setRemember] = useState(false);
    const [remembered, setRemembered] = useState<RememberedAccount | null>(null);
    const serverId = useRef<string | null>(null);
    const autoAttempted = useRef(false);
    const [error, setError] = useState<
        keyof typeof en.errors | 'passwordMismatch' | 'previewNote' | null
    >(null);
    const [paused, setPaused] = useState(
        () => matchMedia('(prefers-reduced-motion: reduce)').matches,
    );
    const [state, setState] = useState<ViewState>({
        generation: 0,
        phase: 'auth',
        connected: !inGame,
        pending: false,
    });
    const t = dictionaries[locale];
    const recorder = useRecorder(state.cameraEditor === true);
    const registering = mode === 'register';
    const entering = state.phase !== 'auth';
    const busy = state.pending || entering;
    const message =
        error === 'passwordMismatch' || error === 'previewNote'
            ? t[error]
            : error
              ? t.errors[error]
              : state.error
                ? t.errors[state.error]
                : null;

    useEffect(
        () =>
            subscribe((next) => {
                if (next.forgetRemembered && next.serverId) {
                    forgetRememberedAccount(next.serverId);
                    setRemembered(null);
                    setRemember(false);
                    autoAttempted.current = true;
                }

                if (next.serverId && next.serverId !== serverId.current) {
                    serverId.current = next.serverId;
                    autoAttempted.current = next.forgetRemembered === true;
                    const saved = loadRememberedAccount(next.serverId);

                    setRemembered(saved);
                    setRemember(Boolean(saved));

                    if (saved) {
                        setUsername(saved.username);
                    }
                }

                if (
                    isRememberedAccount(next.remembered) &&
                    next.remembered.serverId === next.serverId
                ) {
                    saveRememberedAccount(next.remembered);
                    setRemembered(next.remembered);
                    // Acknowledge storage before the server spawns and closes this view.
                    send(EVENT.rememberedReady);
                }

                if (next.error === 'rememberedExpired' && next.serverId) {
                    forgetRememberedAccount(next.serverId);
                    setRemembered(null);
                    setRemember(false);
                }

                setState(next);
            }),
        [],
    );

    useEffect(() => {
        if (
            !inGame ||
            !remembered ||
            !state.connected ||
            state.pending ||
            state.phase !== 'auth' ||
            autoAttempted.current
        ) {
            return;
        }

        autoAttempted.current = true;
        setState((current) => ({ ...current, pending: true, error: undefined }));
        send(EVENT.auth, {
            mode: 'remembered',
            username: remembered.username,
            token: remembered.token,
            locale,
        });
    }, [remembered, state.connected, state.pending, state.phase, locale]);

    useEffect(() => {
        document.documentElement.lang = locale;
        document.title = `${t.brand} — ${t.name}`;

        try {
            localStorage.setItem('lhrp.locale', locale);
        } catch {
            /* Optional preference only. */
        }
    }, [locale, t]);

    useEffect(() => {
        send(EVENT.motion, { reduced: paused });
    }, [paused]);

    function changeMode(next: AuthMode) {
        if (remembered) {
            forgetAccount();
        }

        setMode(next);
        setPassword('');
        setConfirm('');
        setError(null);
        setState((current) => ({ ...current, error: undefined }));
    }

    function forgetAccount() {
        if (remembered) {
            forgetRememberedAccount(remembered.serverId);
            send(EVENT.forget, { token: remembered.token });
        }

        autoAttempted.current = true;
        setRemembered(null);
        setRemember(false);
        setPassword('');
    }

    function submit(event: SubmitEvent) {
        event.preventDefault();

        if (busy || !state.connected) {
            return;
        }

        const request =
            remembered && !registering
                ? {
                      mode: 'remembered' as const,
                      username: remembered.username,
                      token: remembered.token,
                      locale,
                  }
                : { mode, username: username.trim(), password, locale, remember };
        const validation = credentialsError(request);

        if (validation) {
            setError(validation);

            return;
        }

        if (registering && password !== confirm) {
            setError('passwordMismatch');

            return;
        }

        if (!inGame) {
            setError('previewNote');

            return;
        }

        setError(null);
        setState((current) => ({ ...current, pending: true, error: undefined }));
        send(EVENT.auth, request);
        setPassword('');
        setConfirm('');
    }

    if (recorder.active && recorder.state) {
        return <Recorder state={recorder.state} t={t.recorder} />;
    }

    return (
        <div {...stylex.props(styles.page)}>
            {state.cameraEditor && (
                <button
                    type="button"
                    onClick={() => send(EVENT.recorder, { action: 'toggle' })}
                    {...stylex.props(styles.recorderHint)}
                >
                    {t.recorder.hint}
                </button>
            )}
            {!inGame && (
                <div class="preview-city" aria-hidden="true">
                    <div class="skyline skyline-far" />
                    <div class="skyline" />
                </div>
            )}
            <div {...stylex.props(styles.vignette)} aria-hidden="true" />
            <header {...stylex.props(styles.header)}>
                <a
                    {...stylex.props(styles.brand)}
                    href="#"
                    onClick={(event) => event.preventDefault()}
                    aria-label={t.name}
                >
                    <Seal />
                    <span>
                        <strong {...stylex.props(styles.brandName)}>{t.brand}</strong>
                        <span {...stylex.props(styles.brandDetail)}>{t.name}</span>
                    </span>
                </a>
                <nav aria-label={t.language} {...stylex.props(styles.languages)}>
                    {(['en', 'cs'] as const).map((value) => (
                        <button
                            key={value}
                            type="button"
                            lang={value}
                            title={value === 'en' ? t.english : t.czech}
                            aria-label={value === 'en' ? t.english : t.czech}
                            aria-pressed={locale === value}
                            onClick={() => setLocale(value)}
                            {...stylex.props(
                                styles.language,
                                locale === value && styles.selectedLanguage,
                            )}
                        >
                            {t[value]}
                        </button>
                    ))}
                </nav>
            </header>

            <main {...stylex.props(styles.main)}>
                <Panel title="dialog-title">
                    <div {...stylex.props(styles.registry)}>
                        <span>{t.registry}</span>
                        <span {...stylex.props(styles.edition)}>{t.edition}</span>
                    </div>
                    {entering ? (
                        <div {...stylex.props(styles.arrival)} role="status" aria-live="polite">
                            <div {...stylex.props(styles.arrivalSeal)}>
                                <Seal />
                            </div>
                            <h1 id="dialog-title" {...stylex.props(styles.title)}>
                                {t.spawning}
                            </h1>
                            <p {...stylex.props(styles.description)}>
                                {state.returning ? t.returningDescription : t.spawningDescription}
                            </p>
                            <div class="loading-line" />
                        </div>
                    ) : (
                        <>
                            <h1 id="dialog-title" {...stylex.props(styles.title)}>
                                {registering ? t.registerTitle : t.loginTitle}
                            </h1>
                            <p {...stylex.props(styles.description)}>
                                {registering ? t.registerDescription : t.loginDescription}
                            </p>
                            <div {...stylex.props(styles.tabs)}>
                                <button
                                    type="button"
                                    aria-pressed={!registering}
                                    disabled={busy}
                                    onClick={() => changeMode('login')}
                                    {...stylex.props(styles.tab, !registering && styles.activeTab)}
                                >
                                    {t.login}
                                </button>
                                <button
                                    type="button"
                                    aria-pressed={registering}
                                    disabled={busy}
                                    onClick={() => changeMode('register')}
                                    {...stylex.props(styles.tab, registering && styles.activeTab)}
                                >
                                    {t.register}
                                </button>
                            </div>
                            <form
                                onSubmit={submit}
                                noValidate
                                aria-busy={busy}
                                {...stylex.props(styles.form)}
                            >
                                <Field
                                    id="username"
                                    name="username"
                                    label={t.username}
                                    placeholder={t.usernamePlaceholder}
                                    value={username}
                                    onInput={(event) => setUsername(event.currentTarget.value)}
                                    autoComplete="username"
                                    spellcheck={false}
                                    autoCapitalize="none"
                                    maxLength={24}
                                    required
                                    disabled={busy || Boolean(remembered)}
                                    hint={registering ? t.usernameHint : undefined}
                                />
                                {!remembered && (
                                    <Field
                                        id="password"
                                        name="password"
                                        label={t.password}
                                        placeholder={
                                            registering
                                                ? t.newPasswordPlaceholder
                                                : t.passwordPlaceholder
                                        }
                                        type={reveal ? 'text' : 'password'}
                                        value={password}
                                        onInput={(event) => setPassword(event.currentTarget.value)}
                                        autoComplete={
                                            registering ? 'new-password' : 'current-password'
                                        }
                                        maxLength={128}
                                        required
                                        disabled={busy}
                                        hint={registering ? t.passwordHint : undefined}
                                        trailing={
                                            <button
                                                type="button"
                                                disabled={busy}
                                                aria-label={
                                                    reveal ? t.hidePassword : t.showPassword
                                                }
                                                aria-pressed={reveal}
                                                onClick={() => setReveal(!reveal)}
                                                {...stylex.props(styles.eye)}
                                            >
                                                <svg
                                                    width="17"
                                                    height="17"
                                                    viewBox="0 0 24 24"
                                                    fill="none"
                                                    stroke="currentColor"
                                                    stroke-width="1.4"
                                                    aria-hidden="true"
                                                >
                                                    <path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12Z" />
                                                    <circle cx="12" cy="12" r="3" />
                                                    {reveal && <path d="m3 3 18 18" />}
                                                </svg>
                                            </button>
                                        }
                                    />
                                )}
                                {remembered && (
                                    <div {...stylex.props(styles.rememberRow)}>
                                        <span>{t.rememberedAccount}</span>
                                        <button
                                            type="button"
                                            disabled={busy}
                                            onClick={forgetAccount}
                                            {...stylex.props(styles.accountLink)}
                                        >
                                            {t.forgetAccount}
                                        </button>
                                    </div>
                                )}
                                {registering && (
                                    <Field
                                        id="confirm"
                                        name="confirm"
                                        label={t.confirmPassword}
                                        placeholder={t.confirmPlaceholder}
                                        type="password"
                                        value={confirm}
                                        onInput={(event) => setConfirm(event.currentTarget.value)}
                                        autoComplete="new-password"
                                        maxLength={128}
                                        required
                                        disabled={busy}
                                    />
                                )}
                                {message && (
                                    <p role="alert" {...stylex.props(styles.error)}>
                                        {message}
                                    </p>
                                )}
                                {!remembered && (
                                    <label {...stylex.props(styles.rememberRow)}>
                                        <input
                                            type="checkbox"
                                            checked={remember}
                                            disabled={busy}
                                            onChange={(event) =>
                                                setRemember(event.currentTarget.checked)
                                            }
                                            {...stylex.props(styles.checkbox)}
                                        />
                                        {t.rememberMe}
                                    </label>
                                )}
                                <div {...stylex.props(styles.submit)}>
                                    <Button type="submit" disabled={busy || !state.connected}>
                                        {busy
                                            ? t.working
                                            : !state.connected
                                              ? t.connecting
                                              : registering
                                                ? t.registerSubmit
                                                : t.loginSubmit}
                                        {!busy && state.connected && <Arrow />}
                                    </Button>
                                </div>
                            </form>
                        </>
                    )}
                    <p {...stylex.props(styles.note)}>
                        <span {...stylex.props(styles.noteMark)} aria-hidden="true">
                            <svg width="10" height="14" viewBox="0 0 10 14" fill="none">
                                <path d="M5 1 9 7 5 13 1 7Z" stroke="currentColor" />
                            </svg>
                        </span>
                        {registering ? t.randomNote : t.savedNote}
                    </p>
                </Panel>
            </main>

            <footer {...stylex.props(styles.footer)}>
                <div {...stylex.props(styles.city)}>
                    <span {...stylex.props(styles.locationMark)} aria-hidden="true" />
                    <span>
                        {t.city}
                        <span {...stylex.props(styles.era)}>{t.era}</span>
                    </span>
                </div>
                <p {...stylex.props(styles.tagline)}>
                    {t.tagline}
                    <span {...stylex.props(styles.taglineDetail)}>{t.taglineDetail}</span>
                </p>
                <div {...stylex.props(styles.cameraControls)}>
                    <span {...stylex.props(styles.cameraLabel)}>
                        {inGame ? t.cinematic : t.preview}
                    </span>
                    <Button
                        secondary
                        aria-label={paused ? t.resumeCamera : t.pauseCamera}
                        aria-pressed={paused}
                        onClick={() => setPaused(!paused)}
                    >
                        <svg
                            width="12"
                            height="12"
                            viewBox="0 0 12 12"
                            fill="currentColor"
                            aria-hidden="true"
                        >
                            {paused ? (
                                <path d="m3 1 8 5-8 5Z" />
                            ) : (
                                <path d="M2 1h3v10H2zm5 0h3v10H7z" />
                            )}
                        </svg>
                    </Button>
                </div>
            </footer>
        </div>
    );
}

const styles = stylex.create({
    rememberRow: {
        display: 'flex',
        alignItems: 'center',
        gap: 9,
        color: theme.muted,
        fontSize: 11,
        lineHeight: 1.5,
    },
    checkbox: { accentColor: theme.brass, width: 14, height: 14, margin: 0, cursor: 'pointer' },
    accountLink: {
        borderWidth: 0,
        padding: 0,
        backgroundColor: 'transparent',
        color: theme.brass,
        fontFamily: theme.body,
        fontSize: 11,
        cursor: 'pointer',
        textDecoration: 'underline',
    },
    recorderHint: {
        position: 'fixed',
        bottom: 64,
        left: 40,
        zIndex: 2,
        color: theme.brass,
        backgroundColor: 'transparent',
        borderWidth: 0,
        cursor: 'pointer',
        fontSize: 11,
        fontFamily: theme.body,
    },
    page: {
        minHeight: '100dvh',
        display: 'flex',
        flexDirection: 'column',
        color: theme.paper,
        fontFamily: theme.body,
        position: 'relative',
        isolation: 'isolate',
    },
    vignette: {
        position: 'fixed',
        inset: 0,
        zIndex: -1,
        pointerEvents: 'none',
        backgroundImage:
            'radial-gradient(ellipse at center, rgba(8,14,12,.12) 0%, rgba(6,12,10,.65) 100%), linear-gradient(0deg, rgba(5,10,8,.85), transparent 28%, transparent 72%, rgba(5,10,8,.8))',
    },
    header: {
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        padding: '28px 40px',
        gap: 20,
        '@media (max-width: 520px)': { padding: '22px' },
        '@media (max-height: 820px)': { paddingTop: 18, paddingBottom: 18 },
    },
    brand: {
        display: 'flex',
        alignItems: 'center',
        gap: 14,
        color: theme.brass,
        textDecoration: 'none',
    },
    brandName: {
        display: 'block',
        color: theme.paper,
        fontSize: 16,
        fontWeight: 500,
        letterSpacing: '.27em',
    },
    brandDetail: {
        display: 'block',
        color: theme.muted,
        fontSize: 9,
        letterSpacing: '.06em',
        marginTop: 5,
    },
    languages: {
        display: 'flex',
        gap: 2,
        alignItems: 'center',
        borderWidth: 1,
        borderStyle: 'solid',
        borderColor: theme.line,
        padding: 3,
        borderRadius: theme.radius,
    },
    language: {
        backgroundColor: { default: 'transparent', ':hover': 'rgba(255,255,255,.05)' },
        color: theme.muted,
        padding: '7px 9px',
        fontSize: 10,
        fontWeight: 500,
        borderWidth: 0,
        cursor: 'pointer',
        ':focus-visible': { outlineWidth: 1, outlineStyle: 'solid', outlineColor: theme.brass },
    },
    selectedLanguage: { backgroundColor: 'rgba(200,179,130,.14)', color: theme.brass },
    main: {
        flexGrow: 1,
        display: 'flex',
        justifyContent: 'center',
        alignItems: 'center',
        padding: '22px 20px 36px',
        '@media (max-height: 820px)': { paddingTop: 8, paddingBottom: 12 },
    },
    registry: {
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: 16,
        color: theme.brass,
        fontSize: 9,
        letterSpacing: '.14em',
        textTransform: 'uppercase',
        paddingBottom: 24,
        '@media (max-height: 820px)': { paddingBottom: 18 },
    },
    edition: { color: theme.muted, letterSpacing: '.06em', fontSize: 8 },
    title: {
        fontFamily: theme.display,
        fontSize: 42,
        fontWeight: 400,
        lineHeight: 1.1,
        letterSpacing: '-.025em',
        margin: '0 0 12px',
        '@media (max-width: 520px)': { fontSize: 37 },
    },
    description: { color: theme.muted, fontSize: 12, lineHeight: 1.8, margin: 0, maxWidth: 310 },
    tabs: {
        display: 'flex',
        gap: 26,
        borderBottomWidth: 1,
        borderBottomStyle: 'solid',
        borderBottomColor: theme.line,
        marginTop: 28,
        marginBottom: 24,
        '@media (max-height: 820px)': { marginTop: 20, marginBottom: 18 },
    },
    tab: {
        color: { default: theme.muted, ':hover': theme.paper },
        fontFamily: theme.body,
        fontSize: 11,
        padding: '0 0 13px',
        cursor: 'pointer',
        backgroundColor: 'transparent',
        borderWidth: 0,
        borderBottomWidth: 1,
        borderBottomStyle: 'solid',
        borderBottomColor: 'transparent',
        marginBottom: -1,
        ':focus-visible': {
            outlineWidth: 1,
            outlineStyle: 'solid',
            outlineColor: theme.brass,
            outlineOffset: 4,
        },
    },
    activeTab: { color: theme.brass, borderBottomColor: theme.brass },
    form: {
        display: 'flex',
        flexDirection: 'column',
        gap: 20,
        '@media (max-height: 820px)': { gap: 14 },
    },
    eye: {
        backgroundColor: 'transparent',
        color: { default: theme.muted, ':hover': theme.brass },
        borderWidth: 0,
        padding: 13,
        height: '100%',
        cursor: 'pointer',
        display: 'flex',
        alignItems: 'center',
        ':focus-visible': {
            outlineWidth: 1,
            outlineStyle: 'solid',
            outlineColor: theme.brass,
            outlineOffset: -4,
        },
    },
    submit: { marginTop: 4 },
    error: {
        color: theme.danger,
        fontSize: 11,
        lineHeight: 1.7,
        padding: '10px 12px',
        margin: 0,
        backgroundColor: 'rgba(186,80,62,.08)',
        borderLeftWidth: 2,
        borderLeftStyle: 'solid',
        borderLeftColor: theme.danger,
    },
    note: {
        display: 'flex',
        alignItems: 'center',
        gap: 9,
        color: theme.muted,
        fontSize: 9,
        lineHeight: 1.8,
        margin: '22px 0 0',
        paddingTop: 20,
        borderTopWidth: 1,
        borderTopStyle: 'solid',
        borderTopColor: theme.line,
    },
    noteMark: { color: theme.brass, fontSize: 15 },
    arrival: {
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        textAlign: 'center',
        padding: '24px 0 42px',
        gap: 12,
    },
    arrivalSeal: { color: theme.brass, marginBottom: 12 },
    footer: {
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        gap: 24,
        padding: '20px 40px 30px',
        '@media (max-width: 520px)': { padding: '18px 22px', gap: 12 },
        '@media (max-height: 820px)': { paddingTop: 14, paddingBottom: 18 },
    },
    city: {
        display: 'flex',
        gap: 12,
        alignItems: 'center',
        color: theme.paper,
        fontSize: 10,
        minWidth: 160,
    },
    locationMark: {
        width: 5,
        height: 5,
        backgroundColor: theme.brass,
        borderRadius: '50%',
        boxShadow: '0 0 12px rgba(200,179,130,.4)',
    },
    era: { display: 'block', marginTop: 6, color: theme.muted, fontSize: 9 },
    tagline: {
        fontFamily: theme.display,
        fontStyle: 'italic',
        fontSize: 19,
        margin: 0,
        color: theme.paper,
        textAlign: 'center',
        '@media (max-width: 800px)': { display: 'none' },
    },
    taglineDetail: { color: theme.muted, marginLeft: 5 },
    cameraControls: {
        display: 'flex',
        alignItems: 'center',
        gap: 14,
        minWidth: 160,
        justifyContent: 'flex-end',
        '@media (max-width: 520px)': { minWidth: 0 },
    },
    cameraLabel: {
        fontSize: 9,
        color: theme.muted,
        '@media (max-width: 520px)': { display: 'none' },
    },
});
