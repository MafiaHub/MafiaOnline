export const RESOURCE = 'lost-heaven-roleplay';
export const EVENT = {
    ready: 'real-life:ready',
    auth: 'real-life:auth',
    rememberedReady: 'real-life:remembered-ready',
    forget: 'real-life:forget',
    state: 'real-life:state',
    motion: 'real-life:motion',
    cameraPaths: 'real-life:camera-paths',
    cameraSave: 'real-life:camera-save',
    cameraSaved: 'real-life:camera-saved',
    recorder: 'real-life:recorder',
    recorderState: 'real-life:recorder-state',
} as const;

export type Locale = 'en' | 'cs';
export type AuthMode = 'login' | 'register';
export type ErrorCode =
    | 'invalidUsername'
    | 'invalidPassword'
    | 'invalidCredentials'
    | 'rememberedExpired'
    | 'banned'
    | 'usernameTaken'
    | 'alreadyOnline'
    | 'rateLimited'
    | 'unavailable'
    | 'notReady'
    | 'timeout';

export interface Point {
    x: number;
    y: number;
    z: number;
}
export interface SavedPlace {
    mission: string;
    position: Point;
    heading: number;
}
export type AuthRequest = {
    generation: number;
    username: string;
    remember?: boolean;
    locale: Locale;
} & ({ mode: AuthMode; password: string } | { mode: 'remembered'; token: string });
export interface RememberedAccount {
    serverId: string;
    username: string;
    token: string;
    expiresAt: number;
}
export interface SessionState {
    generation: number;
    phase: 'auth' | 'spawning' | 'playing';
    username?: string;
    model?: string;
    returning?: boolean;
    error?: ErrorCode;
    cameraEditor?: boolean;
    serverId?: string;
    remembered?: RememberedAccount;
    forgetRemembered?: boolean;
}
export interface ViewState extends SessionState {
    connected: boolean;
    pending: boolean;
}

export const MODELS = [
    { file: 'Tommy.i3d', key: 'tommy' },
    { file: 'Paulie.i3d', key: 'paulie' },
    { file: 'Hoolig02.i3d', key: 'outsider' },
    { file: 'Pol01.i3d', key: 'officer' },
] as const;

export function isRecord(value: unknown): value is Record<string, unknown> {
    return typeof value === 'object' && value !== null && !Array.isArray(value);
}

export function isLocale(value: unknown): value is Locale {
    return value === 'en' || value === 'cs';
}

export function isModel(value: unknown): value is string {
    return MODELS.some((model) => model.file === value);
}

export function isPoint(value: unknown): value is Point {
    return (
        isRecord(value) &&
        [value.x, value.y, value.z].every(
            (n) => typeof n === 'number' && Number.isFinite(n) && Math.abs(n) <= 50_000,
        )
    );
}

export function credentialsError(value: unknown): ErrorCode | null {
    if (
        !isRecord(value) ||
        typeof value.username !== 'string' ||
        !/^[a-zA-Z0-9_]{3,24}$/.test(value.username)
    ) {
        return 'invalidUsername';
    }

    if (value.mode === 'remembered') {
        if (!isRememberToken(value.token)) {
            return 'rememberedExpired';
        }
    } else {
        if (
            typeof value.password !== 'string' ||
            value.password.length < 10 ||
            value.password.length > 128
        ) {
            return 'invalidPassword';
        }
    }

    if (
        !['login', 'register', 'remembered'].includes(String(value.mode)) ||
        !isLocale(value.locale) ||
        (value.remember !== undefined && typeof value.remember !== 'boolean')
    ) {
        return 'unavailable';
    }

    return null;
}

export function isRememberToken(value: unknown): value is string {
    return typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
}

export function isRememberedAccount(value: unknown): value is RememberedAccount {
    return (
        isRecord(value) &&
        typeof value.serverId === 'string' &&
        /^[a-f0-9-]{36}$/.test(value.serverId) &&
        typeof value.username === 'string' &&
        /^[a-zA-Z0-9_]{3,24}$/.test(value.username) &&
        isRememberToken(value.token) &&
        typeof value.expiresAt === 'number' &&
        Number.isSafeInteger(value.expiresAt)
    );
}
