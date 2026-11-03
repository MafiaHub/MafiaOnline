// Free Ride accounts: a username and password per player, and "remember me"
// tokens. Only node:crypto is used here; the caller supplies where the
// accounts document is loaded from and saved to (lib/store.ts), so the rules
// can be exercised outside the server.
//
// Passwords: scrypt (N 2^15, r 8, p 1, 64-byte key) with a random 16-byte
// salt per account, compared with crypto.timingSafeEqual. An unknown name
// takes the same expensive path as a wrong password. The cost is stored with
// each hash, so it can be raised later without invalidating old accounts.
//
// Remember tokens: "<id>.<secret>", 32 random bytes of secret. The server
// keeps only SHA-256(secret), the hardware ID it was issued to, and an expiry
// (30 days). Every use replaces the token with a new one, an account keeps at
// most five, and they can be revoked one by one (log out) or all at once.
// A replaced token is remembered until it would have expired: presenting it
// again means a copy is in other hands, so every token of the account goes.
//
// Player records: an account owns one profile record key (lib/profiles.ts).
// Records from before accounts were keyed "<nickname>@<hardware ID>"; the
// first registration of that name from that same machine claims the record,
// so a returning player keeps their progress. A record from another machine
// is never claimed by a name alone; the console can move one over.
import * as crypto from 'node:crypto';

const DOCUMENT_VERSION = 1;

export const SCRYPT = Object.freeze({ N: 1 << 15, r: 8, p: 1, keylen: 64 });
const SCRYPT_MAXMEM = 64 * 1024 * 1024;
// scrypt runs on the libuv pool; more than this at once wait their turn.
const MAX_CONCURRENT_HASHES = 2;

export const TOKEN_TTL_MS = 30 * 24 * 60 * 60_000;
export const MAX_TOKENS = 5;
const NAME_PATTERN = /^[A-Za-z0-9_.\- ]+$/;
const NAME_MIN = 3;
const NAME_MAX = 24;
const PASSWORD_MIN = 6;
const PASSWORD_MAX = 128;
const MINUTE = 60_000;
// Replaced tokens remembered per account, for spotting a copy.
const MAX_SPENT = 20;

// [attempts, window]: failed logins for one name from one address, for one
// name from anywhere (high and short, so nobody can lock another player out
// for long), and per address; failed token uses per address; new accounts
// per address.
export const LIMITS = Object.freeze({
    loginNameAddress: [5, 15 * MINUTE],
    loginName: [50, 5 * MINUTE],
    loginAddress: [20, 15 * MINUTE],
    tokenAddress: [10, 15 * MINUTE],
    registerAddress: [3, 60 * MINUTE],
} satisfies Record<string, readonly [number, number]>);

type LimitKind = keyof typeof LIMITS;

/** A rate-limit bucket: its kind and what it counts for (a name, an address or both). */
type Bucket = `${LimitKind}:${string}`;

/** A remember token as stored: never its secret. */
export interface StoredToken {
    id: string;
    /** SHA-256 of the secret, hex. */
    hash: string;
    hardwareId: string;
    created: number;
    expires: number;
}

/** A replaced token, kept to recognise a copy of it. */
export interface SpentToken {
    id: string;
    hash: string;
    expires: number;
}

export interface Account {
    /** As the player typed it. */
    name: string;
    /** The player record it owns (lib/profiles.ts). */
    key: string;
    /** "scrypt$N$r$p$salt$key", base64. */
    password: string;
    created: number;
    lastLogin: number;
    tokens: StoredToken[];
    spent: SpentToken[];
}

export interface AccountsDocument {
    version: number;
    serverId: string;
    /** By lower-cased name. */
    accounts: Record<string, Account>;
}

export interface AccountsOptions {
    load(): Partial<AccountsDocument> | null;
    save(document: AccountsDocument): void;
    now?(): number;
}

/** Why a request was turned down, and which form field to show it under. */
export interface Refusal {
    ok: false;
    field?: 'name' | 'password' | 'form';
    message: string;
    code?: 'limited' | 'expired';
    limited?: true;
    expired?: true;
    /** The token was a copy of one already used: every login of the account was revoked. */
    stolen?: true;
}

export type NameCheck = { ok: true; name: string; id: string } | Refusal;
export type PasswordCheck = { ok: true } | Refusal;
export type LoginResult = { ok: true; account: Account } | Refusal;
export type RegisterResult = { ok: true; account: Account; claimed: boolean } | Refusal;

/** A new remember token, to hand to the client. */
export interface IssuedToken {
    token: string;
    expires: number;
}

export type TokenResult = ({ ok: true; account: Account } & IssuedToken) | Refusal;

/** The record a registering player would have had before accounts. */
export interface Claim {
    key: string;
    exists(key: string): boolean;
}

export interface AccountSummary {
    name: string;
    key: string;
    created: number;
    lastLogin: number;
    tokens: number;
}

function lower(name: string): string {
    return name.toLowerCase();
}

function normalName(name: string): string {
    return name.trim().replace(/\s+/g, ' ');
}

// Display names follow the server's nickname policy closely enough that
// setNickname keeps them as typed: letters, digits, space, _ . -, 3 to 24.
export function checkName(input: unknown): NameCheck {
    const name = typeof input === 'string' ? normalName(input) : '';

    if (name.length < NAME_MIN) {
        return {
            ok: false,
            field: 'name',
            message: `A name needs at least ${NAME_MIN} characters.`,
        };
    }

    if (name.length > NAME_MAX) {
        return {
            ok: false,
            field: 'name',
            message: `A name can have at most ${NAME_MAX} characters.`,
        };
    }

    if (!NAME_PATTERN.test(name)) {
        return {
            ok: false,
            field: 'name',
            message: 'Use letters, digits, spaces, dots, dashes or underscores.',
        };
    }

    if (!/[A-Za-z]/.test(name)) {
        return { ok: false, field: 'name', message: 'A name needs at least one letter.' };
    }

    // Players are "Guest <id>" until they log in.
    if (/^guest([\s\d._-]|$)/i.test(name)) {
        return { ok: false, field: 'name', message: 'Names starting with "Guest" are reserved.' };
    }

    return { ok: true, name, id: lower(name) };
}

export function checkPassword(password: unknown, name?: string): PasswordCheck {
    if (typeof password !== 'string' || password.length < PASSWORD_MIN) {
        return {
            ok: false,
            field: 'password',
            message: `A password needs at least ${PASSWORD_MIN} characters.`,
        };
    }

    if (password.length > PASSWORD_MAX) {
        return {
            ok: false,
            field: 'password',
            message: `A password can have at most ${PASSWORD_MAX} characters.`,
        };
    }

    if (name && lower(password) === lower(name)) {
        return { ok: false, field: 'password', message: 'Your password cannot be your name.' };
    }

    return { ok: true };
}

function sha256(text: string): Buffer {
    return crypto.createHash('sha256').update(text).digest();
}

function minutesLeft(ms: number): string {
    const minutes = Math.max(1, Math.ceil(ms / MINUTE));

    return minutes === 1 ? 'a minute' : `${minutes} minutes`;
}

function limitOf(bucket: Bucket): readonly [number, number] {
    return LIMITS[bucket.slice(0, bucket.indexOf(':')) as LimitKind];
}

// A token string's id and secret, or null for one that cannot be a token.
function parseToken(token: unknown): { id: string; secret: string } | null {
    if (typeof token !== 'string' || token.length > 128) {
        return null;
    }

    const [id, secret, extra] = token.split('.');

    return id && secret && extra === undefined ? { id, secret } : null;
}

function matches(secret: string, hash: string): boolean {
    return crypto.timingSafeEqual(sha256(secret), Buffer.from(hash, 'hex'));
}

const WRONG_LOGIN: Refusal = { ok: false, field: 'form', message: 'Wrong name or password.' };
const NAME_TAKEN: Refusal = {
    ok: false,
    field: 'name',
    message: 'That name is taken. Log in, or pick another.',
};
const NO_ACCOUNT: Refusal = { ok: false, message: 'No such account.' };

/** Every account of this server and its tokens, kept in one document. */
export class Accounts {
    readonly serverId: string;
    private readonly state: AccountsDocument;
    private readonly persistDocument: (document: AccountsDocument) => void;
    private readonly now: () => number;
    private hashing = 0;
    private readonly waiting: (() => void)[] = [];
    private readonly attempts = new Map<Bucket, number[]>();
    private readonly tokenIndex = new Map<string, Account>();
    private readonly spentIndex = new Map<string, Account>();

    constructor({ load, save, now = Date.now }: AccountsOptions) {
        this.persistDocument = save;
        this.now = now;
        const doc = load() ?? {};

        this.state = {
            version: DOCUMENT_VERSION,
            serverId:
                typeof doc.serverId === 'string' && doc.serverId
                    ? doc.serverId
                    : crypto.randomBytes(12).toString('base64url'),
            accounts: doc.accounts && typeof doc.accounts === 'object' ? doc.accounts : {},
        };

        this.serverId = this.state.serverId;

        for (const account of Object.values(this.state.accounts)) {
            account.tokens = (account.tokens ?? []).filter((token) => token.expires > now());
            account.spent = (account.spent ?? []).filter((token) => token.expires > now());

            for (const token of account.tokens) {
                this.tokenIndex.set(token.id, account);
            }

            for (const token of account.spent) {
                this.spentIndex.set(token.id, account);
            }
        }

        this.persist();
    }

    private persist(): void {
        this.persistDocument({
            version: this.state.version,
            serverId: this.state.serverId,
            accounts: this.state.accounts,
        });
    }

    // ---- hashing -------------------------------------------------------

    private async withHashSlot<T>(work: () => Promise<T>): Promise<T> {
        if (this.hashing >= MAX_CONCURRENT_HASHES) {
            await new Promise<void>((resolve) => this.waiting.push(resolve));
        }

        this.hashing += 1;

        try {
            return await work();
        } finally {
            this.hashing -= 1;
            this.waiting.shift()?.();
        }
    }

    private derive(
        password: string,
        salt: Buffer,
        { N, r, p, keylen }: { N: number; r: number; p: number; keylen: number },
    ): Promise<Buffer> {
        return this.withHashSlot(
            () =>
                new Promise<Buffer>((resolve, reject) => {
                    crypto.scrypt(
                        password.normalize('NFC'),
                        salt,
                        keylen,
                        { N, r, p, maxmem: SCRYPT_MAXMEM },
                        (error, key) => (error ? reject(error) : resolve(key)),
                    );
                }),
        );
    }

    private async hashPassword(password: string): Promise<string> {
        const salt = crypto.randomBytes(16);
        const key = await this.derive(password, salt, SCRYPT);

        return `scrypt$${SCRYPT.N}$${SCRYPT.r}$${SCRYPT.p}$${salt.toString('base64')}$${key.toString('base64')}`;
    }

    // A missing hash still costs one derivation, so a wrong name and a wrong
    // password take the same time.
    private async verifyPassword(password: string, encoded: string | undefined): Promise<boolean> {
        const parts = (encoded ?? '').split('$');
        const valid = parts.length === 6 && parts[0] === 'scrypt';
        const [N, r, p] = valid ? parts.slice(1, 4).map(Number) : [SCRYPT.N, SCRYPT.r, SCRYPT.p];
        const salt = valid ? Buffer.from(parts[4], 'base64') : Buffer.alloc(16);
        const expected = valid ? Buffer.from(parts[5], 'base64') : Buffer.alloc(SCRYPT.keylen);
        const key = await this.derive(password, salt, {
            N,
            r,
            p,
            keylen: expected.length || SCRYPT.keylen,
        });

        return crypto.timingSafeEqual(key, expected) && valid;
    }

    // ---- rate limits ---------------------------------------------------

    private recent(bucket: Bucket): number[] {
        const [, window] = limitOf(bucket);
        const list = (this.attempts.get(bucket) ?? []).filter((at) => this.now() - at < window);

        if (list.length) {
            this.attempts.set(bucket, list);
        } else {
            this.attempts.delete(bucket);
        }

        return list;
    }

    // Milliseconds until the first of these buckets allows another attempt.
    private blockedFor(buckets: readonly Bucket[]): number {
        let wait = 0;

        for (const bucket of buckets) {
            const [limit, window] = limitOf(bucket);
            const list = this.recent(bucket);

            if (list.length >= limit) {
                wait = Math.max(wait, list[list.length - limit] + window - this.now());
            }
        }

        return wait;
    }

    private note(buckets: readonly Bucket[]): void {
        for (const bucket of buckets) {
            this.attempts.set(bucket, [...this.recent(bucket), this.now()]);
        }
    }

    private tooMany(wait: number): Refusal {
        return {
            ok: false,
            field: 'form',
            code: 'limited',
            limited: true,
            message: `Too many attempts. Try again in ${minutesLeft(wait)}.`,
        };
    }

    // ---- accounts ------------------------------------------------------

    checkName(input: unknown): NameCheck {
        return checkName(input);
    }

    checkPassword(password: unknown, name?: string): PasswordCheck {
        return checkPassword(password, name);
    }

    byName(name: unknown): Account | null {
        return typeof name === 'string'
            ? (this.state.accounts[lower(normalName(name))] ?? null)
            : null;
    }

    byKey(key: string): Account | null {
        return Object.values(this.state.accounts).find((account) => account.key === key) ?? null;
    }

    /** claim: the record this player would have had before accounts, taken over when it exists and nobody owns it. */
    async register({
        name,
        password,
        address = '',
        claim = null,
    }: {
        name: unknown;
        password: unknown;
        address?: string;
        claim?: Claim | null;
    }): Promise<RegisterResult> {
        const checked = checkName(name);

        if (!checked.ok) {
            return checked;
        }

        const passwordCheck = checkPassword(password, checked.name);

        if (!passwordCheck.ok) {
            return passwordCheck;
        }

        // checkPassword let only a string through.
        const secret = String(password);
        const buckets: Bucket[] = [`registerAddress:${address}`];
        const wait = this.blockedFor(buckets);

        if (wait > 0) {
            return this.tooMany(wait);
        }

        if (this.state.accounts[checked.id]) {
            return NAME_TAKEN;
        }

        const hash = await this.hashPassword(secret);

        // Another registration may have finished while this one hashed.
        if (this.state.accounts[checked.id]) {
            return NAME_TAKEN;
        }

        const claimed = Boolean(claim?.key && claim.exists(claim.key) && !this.byKey(claim.key));
        const account: Account = {
            name: checked.name,
            key: claimed && claim ? claim.key : `acct:${checked.id}`,
            password: hash,
            created: this.now(),
            lastLogin: this.now(),
            tokens: [],
            spent: [],
        };

        this.state.accounts[checked.id] = account;
        this.note(buckets);
        this.persist();

        return { ok: true, account, claimed };
    }

    /** logoutOthers: every remember token of the account goes too. */
    async login({
        name,
        password,
        address = '',
        logoutOthers = false,
    }: {
        name: unknown;
        password: unknown;
        address?: string;
        logoutOthers?: boolean;
    }): Promise<LoginResult> {
        const checked = checkName(name);

        if (
            !checked.ok ||
            typeof password !== 'string' ||
            !password ||
            password.length > PASSWORD_MAX
        ) {
            return WRONG_LOGIN;
        }

        const buckets: Bucket[] = [
            `loginNameAddress:${checked.id}|${address}`,
            `loginName:${checked.id}`,
            `loginAddress:${address}`,
        ];
        const wait = this.blockedFor(buckets);

        if (wait > 0) {
            return this.tooMany(wait);
        }

        const account = this.state.accounts[checked.id] ?? null;
        const valid = await this.verifyPassword(password, account?.password);

        if (!valid || !account) {
            this.note(buckets);

            return WRONG_LOGIN;
        }

        this.attempts.delete(`loginNameAddress:${checked.id}|${address}`);
        account.lastLogin = this.now();

        if (logoutOthers) {
            this.revokeEvery(account);
        }

        this.persist();

        return { ok: true, account };
    }

    issueToken(account: Account, hardwareId = ''): IssuedToken {
        const id = crypto.randomBytes(9).toString('base64url');
        const secret = crypto.randomBytes(32).toString('base64url');

        account.tokens = account.tokens.filter((token) => token.expires > this.now());
        account.tokens.push({
            id,
            hash: sha256(secret).toString('hex'),
            hardwareId,
            created: this.now(),
            expires: this.now() + TOKEN_TTL_MS,
        });

        while (account.tokens.length > MAX_TOKENS) {
            const oldest = account.tokens.shift();

            if (oldest) {
                this.tokenIndex.delete(oldest.id);
            }
        }

        this.tokenIndex.set(id, account);
        this.persist();

        return { token: `${id}.${secret}`, expires: this.now() + TOKEN_TTL_MS };
    }

    // The account and the stored token entry a token string names, if valid.
    private find(token: unknown): { account: Account; entry: StoredToken } | null {
        const parsed = parseToken(token);
        const account = parsed && this.tokenIndex.get(parsed.id);
        const entry = account?.tokens.find((candidate) => candidate.id === parsed?.id);

        return parsed && account && entry && matches(parsed.secret, entry.hash)
            ? { account, entry }
            : null;
    }

    private drop(account: Account, id: string): void {
        account.tokens = account.tokens.filter((token) => token.id !== id);
        this.tokenIndex.delete(id);
    }

    private revokeEvery(account: Account): number {
        const count = account.tokens.length;

        for (const token of [...account.tokens]) {
            this.drop(account, token.id);
        }

        return count;
    }

    // Keeps a replaced token's id and hash until it would have expired.
    private spend(account: Account, entry: StoredToken): void {
        this.drop(account, entry.id);
        account.spent = [
            ...account.spent.filter((token) => token.expires > this.now()),
            { id: entry.id, hash: entry.hash, expires: entry.expires },
        ];

        while (account.spent.length > MAX_SPENT) {
            const oldest = account.spent.shift();

            if (oldest) {
                this.spentIndex.delete(oldest.id);
            }
        }

        this.spentIndex.set(entry.id, account);
    }

    // A replaced token shown again, with its real secret.
    private replayed(token: unknown): Account | null {
        if (typeof token !== 'string' || token.length > 128) {
            return null;
        }

        const [id, secret] = token.split('.');
        const account = id && secret ? this.spentIndex.get(id) : undefined;
        const entry = account?.spent.find((candidate) => candidate.id === id);

        return account && entry && matches(secret, entry.hash) ? account : null;
    }

    /** Logs in with a remember token and hands back its replacement. */
    useToken({
        token,
        address = '',
        hardwareId = '',
    }: {
        token: unknown;
        address?: string;
        hardwareId?: string;
    }): TokenResult {
        const buckets: Bucket[] = [`tokenAddress:${address}`];
        const wait = this.blockedFor(buckets);

        if (wait > 0) {
            return this.tooMany(wait);
        }

        const found = this.find(token);
        const copied = found ? null : this.replayed(token);

        if (copied) {
            this.revokeEvery(copied);
            this.persist();
            this.note(buckets);

            return {
                ok: false,
                field: 'form',
                code: 'expired',
                expired: true,
                stolen: true,
                message:
                    'This saved login was already used on another computer, so every saved login of this account was signed out. Please log in.',
            };
        }

        const expired = found !== null && found.entry.expires <= this.now();
        const elsewhere =
            found !== null &&
            Boolean(found.entry.hardwareId) &&
            found.entry.hardwareId !== hardwareId;

        if (!found || expired || elsewhere) {
            if (found) {
                this.drop(found.account, found.entry.id);
                this.persist();
            }

            this.note(buckets);

            return {
                ok: false,
                field: 'form',
                code: 'expired',
                expired: true,
                message: 'Your saved login has expired. Please log in again.',
            };
        }

        this.spend(found.account, found.entry);
        found.account.lastLogin = this.now();
        const next = this.issueToken(found.account, hardwareId);

        return { ok: true, account: found.account, ...next };
    }

    /** The account a valid token belongs to, without using it up. */
    ownerOf(token: unknown): Account | null {
        const found = this.find(token);

        return found && found.entry.expires > this.now() ? found.account : null;
    }

    /** Holding a token is enough to throw it away. */
    revokeToken(token: unknown): boolean {
        const found = this.find(token);

        if (!found) {
            return false;
        }

        this.drop(found.account, found.entry.id);
        this.persist();

        return true;
    }

    revokeAll(name: unknown): number {
        const account = this.byName(name);

        if (!account) {
            return 0;
        }

        const count = this.revokeEvery(account);

        this.persist();

        return count;
    }

    async setPassword(name: unknown, password: unknown): Promise<PasswordCheck> {
        const account = this.byName(name);

        if (!account) {
            return NO_ACCOUNT;
        }

        const check = checkPassword(password, account.name);

        if (!check.ok) {
            return check;
        }

        account.password = await this.hashPassword(String(password));
        this.revokeEvery(account);
        this.persist();

        return { ok: true };
    }

    /** Moves an account onto another record (console only). */
    assignRecord(name: unknown, key: string): LoginResult {
        const account = this.byName(name);

        if (!account) {
            return NO_ACCOUNT;
        }

        const owner = this.byKey(key);

        if (owner && owner !== account) {
            return { ok: false, message: `That record belongs to ${owner.name}.` };
        }

        account.key = key;
        this.persist();

        return { ok: true, account };
    }

    list(): AccountSummary[] {
        return Object.values(this.state.accounts).map(
            ({ name, key, created, lastLogin, tokens }) => ({
                name,
                key,
                created,
                lastLogin,
                tokens: tokens.length,
            }),
        );
    }
}

export function createAccounts(options: AccountsOptions): Accounts {
    return new Accounts(options);
}
