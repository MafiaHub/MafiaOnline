// Free Ride accounts: a username and password per player, and "remember me"
// tokens. Only node:crypto is used here; the caller supplies where the
// accounts document is loaded from and saved to (lib/store.js), so the rules
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
// Player records: an account owns one profile record key (lib/profiles.js).
// Records from before accounts were keyed "<nickname>@<hardware ID>"; the
// first registration of that name from that same machine claims the record,
// so a returning player keeps their progress. A record from another machine
// is never claimed by a name alone; the console can move one over.
const crypto = require("node:crypto");

const DOCUMENT_VERSION = 1;
const SCRYPT = {N: 1 << 15, r: 8, p: 1, keylen: 64};
const SCRYPT_MAXMEM = 64 * 1024 * 1024;
// scrypt runs on the libuv pool; more than this at once wait their turn.
const MAX_CONCURRENT_HASHES = 2;
const TOKEN_TTL_MS = 30 * 24 * 60 * 60_000;
const MAX_TOKENS = 5;
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
const LIMITS = {
    loginNameAddress: [5, 15 * MINUTE],
    loginName: [50, 5 * MINUTE],
    loginAddress: [20, 15 * MINUTE],
    tokenAddress: [10, 15 * MINUTE],
    registerAddress: [3, 60 * MINUTE],
};

function lower(name) {
    return name.toLowerCase();
}

// Display names follow the server's nickname policy closely enough that
// setNickname keeps them as typed: letters, digits, space, _ . -, 3 to 24.
function checkName(input) {
    const name = typeof input === "string" ? input.trim().replace(/\s+/g, " ") : "";
    if (name.length < NAME_MIN) return {ok: false, field: "name", message: `A name needs at least ${NAME_MIN} characters.`};
    if (name.length > NAME_MAX) return {ok: false, field: "name", message: `A name can have at most ${NAME_MAX} characters.`};
    if (!NAME_PATTERN.test(name)) return {ok: false, field: "name", message: "Use letters, digits, spaces, dots, dashes or underscores."};
    if (!/[A-Za-z]/.test(name)) return {ok: false, field: "name", message: "A name needs at least one letter."};
    // Players are "Guest <id>" until they log in.
    if (/^guest([\s\d._-]|$)/i.test(name)) return {ok: false, field: "name", message: "Names starting with \"Guest\" are reserved."};
    return {ok: true, name, id: lower(name)};
}

function checkPassword(password, name) {
    if (typeof password !== "string" || password.length < PASSWORD_MIN) {
        return {ok: false, field: "password", message: `A password needs at least ${PASSWORD_MIN} characters.`};
    }
    if (password.length > PASSWORD_MAX) return {ok: false, field: "password", message: `A password can have at most ${PASSWORD_MAX} characters.`};
    if (name && lower(password) === lower(name)) return {ok: false, field: "password", message: "Your password cannot be your name."};
    return {ok: true};
}

function sha256(text) {
    return crypto.createHash("sha256").update(text).digest();
}

function minutesLeft(ms) {
    const minutes = Math.max(1, Math.ceil(ms / MINUTE));
    return minutes === 1 ? "a minute" : `${minutes} minutes`;
}

// options: {load() -> document | null, save(document), now?() -> ms}
function createAccounts({load, save, now = Date.now}) {
    let hashing = 0;
    const waiting = [];
    const attempts = new Map();
    const tokenIndex = new Map();
    const spentIndex = new Map();

    const doc = load() ?? {};
    const state = {
        version: DOCUMENT_VERSION,
        serverId: typeof doc.serverId === "string" && doc.serverId ? doc.serverId : crypto.randomBytes(12).toString("base64url"),
        accounts: doc.accounts && typeof doc.accounts === "object" ? doc.accounts : {},
    };
    for (const account of Object.values(state.accounts)) {
        account.tokens = (account.tokens ?? []).filter((token) => token.expires > now());
        account.spent = (account.spent ?? []).filter((token) => token.expires > now());
        for (const token of account.tokens) tokenIndex.set(token.id, account);
        for (const token of account.spent) spentIndex.set(token.id, account);
    }
    persist();

    function persist() {
        save({version: state.version, serverId: state.serverId, accounts: state.accounts});
    }

    // ---- hashing -------------------------------------------------------

    async function withHashSlot(work) {
        if (hashing >= MAX_CONCURRENT_HASHES) await new Promise((resolve) => waiting.push(resolve));
        hashing += 1;
        try {
            return await work();
        } finally {
            hashing -= 1;
            waiting.shift()?.();
        }
    }

    function derive(password, salt, {N, r, p, keylen}) {
        return withHashSlot(() => new Promise((resolve, reject) => {
            crypto.scrypt(password.normalize("NFC"), salt, keylen, {N, r, p, maxmem: SCRYPT_MAXMEM},
                (error, key) => (error ? reject(error) : resolve(key)));
        }));
    }

    async function hashPassword(password) {
        const salt = crypto.randomBytes(16);
        const key = await derive(password, salt, SCRYPT);
        return `scrypt$${SCRYPT.N}$${SCRYPT.r}$${SCRYPT.p}$${salt.toString("base64")}$${key.toString("base64")}`;
    }

    // A missing hash still costs one derivation, so a wrong name and a wrong
    // password take the same time.
    async function verifyPassword(password, encoded) {
        const parts = (encoded ?? "").split("$");
        const valid = parts.length === 6 && parts[0] === "scrypt";
        const [N, r, p] = valid ? parts.slice(1, 4).map(Number) : [SCRYPT.N, SCRYPT.r, SCRYPT.p];
        const salt = valid ? Buffer.from(parts[4], "base64") : Buffer.alloc(16);
        const expected = valid ? Buffer.from(parts[5], "base64") : Buffer.alloc(SCRYPT.keylen);
        const key = await derive(password, salt, {N, r, p, keylen: expected.length || SCRYPT.keylen});
        return crypto.timingSafeEqual(key, expected) && valid;
    }

    // ---- rate limits ---------------------------------------------------

    function recent(bucket) {
        const [, window] = LIMITS[bucket.split(":")[0]];
        const list = (attempts.get(bucket) ?? []).filter((at) => now() - at < window);
        if (list.length) attempts.set(bucket, list);
        else attempts.delete(bucket);
        return list;
    }

    // Milliseconds until the first of these buckets allows another attempt.
    function blockedFor(buckets) {
        let wait = 0;
        for (const bucket of buckets) {
            const [limit, window] = LIMITS[bucket.split(":")[0]];
            const list = recent(bucket);
            if (list.length >= limit) wait = Math.max(wait, list[list.length - limit] + window - now());
        }
        return wait;
    }

    function note(buckets) {
        for (const bucket of buckets) attempts.set(bucket, [...recent(bucket), now()]);
    }

    function tooMany(wait) {
        return {ok: false, field: "form", code: "limited", limited: true, message: `Too many attempts. Try again in ${minutesLeft(wait)}.`};
    }

    // ---- accounts ------------------------------------------------------

    function byName(name) {
        return typeof name === "string" ? state.accounts[lower(name.trim().replace(/\s+/g, " "))] ?? null : null;
    }

    function byKey(key) {
        return Object.values(state.accounts).find((account) => account.key === key) ?? null;
    }

    // claim: {key, exists(key) -> bool}; the record this player would have
    // had before accounts, taken over when it exists and nobody owns it.
    async function register({name, password, address = "", claim = null}) {
        const checked = checkName(name);
        if (!checked.ok) return checked;
        const passwordCheck = checkPassword(password, checked.name);
        if (!passwordCheck.ok) return passwordCheck;
        const buckets = [`registerAddress:${address}`];
        const wait = blockedFor(buckets);
        if (wait > 0) return tooMany(wait);
        if (state.accounts[checked.id]) return {ok: false, field: "name", message: "That name is taken. Log in, or pick another."};

        const hash = await hashPassword(password);
        // Another registration may have finished while this one hashed.
        if (state.accounts[checked.id]) return {ok: false, field: "name", message: "That name is taken. Log in, or pick another."};
        const claimed = Boolean(claim && claim.key && claim.exists(claim.key) && !byKey(claim.key));
        const account = {name: checked.name, key: claimed ? claim.key : `acct:${checked.id}`, password: hash,
            created: now(), lastLogin: now(), tokens: [], spent: []};
        state.accounts[checked.id] = account;
        note(buckets);
        persist();
        return {ok: true, account, claimed};
    }

    // logoutOthers: every remember token of the account goes too.
    async function login({name, password, address = "", logoutOthers = false}) {
        const checked = checkName(name);
        if (!checked.ok || typeof password !== "string" || !password || password.length > PASSWORD_MAX) {
            return {ok: false, field: "form", message: "Wrong name or password."};
        }
        const buckets = [`loginNameAddress:${checked.id}|${address}`, `loginName:${checked.id}`, `loginAddress:${address}`];
        const wait = blockedFor(buckets);
        if (wait > 0) return tooMany(wait);
        const account = state.accounts[checked.id] ?? null;
        const valid = await verifyPassword(password, account?.password);
        if (!valid || !account) {
            note(buckets);
            return {ok: false, field: "form", message: "Wrong name or password."};
        }
        attempts.delete(`loginNameAddress:${checked.id}|${address}`);
        account.lastLogin = now();
        if (logoutOthers) revokeEvery(account);
        persist();
        return {ok: true, account};
    }

    function issueToken(account, hardwareId = "") {
        const id = crypto.randomBytes(9).toString("base64url");
        const secret = crypto.randomBytes(32).toString("base64url");
        account.tokens = account.tokens.filter((token) => token.expires > now());
        account.tokens.push({id, hash: sha256(secret).toString("hex"), hardwareId, created: now(), expires: now() + TOKEN_TTL_MS});
        while (account.tokens.length > MAX_TOKENS) tokenIndex.delete(account.tokens.shift().id);
        tokenIndex.set(id, account);
        persist();
        return {token: `${id}.${secret}`, expires: now() + TOKEN_TTL_MS};
    }

    // The account and the stored token entry a token string names, if valid.
    function find(token) {
        if (typeof token !== "string" || token.length > 128) return null;
        const [id, secret, extra] = token.split(".");
        if (!id || !secret || extra !== undefined) return null;
        const account = tokenIndex.get(id);
        const entry = account?.tokens.find((candidate) => candidate.id === id);
        if (!entry) return null;
        const valid = crypto.timingSafeEqual(sha256(secret), Buffer.from(entry.hash, "hex"));
        return valid ? {account, entry} : null;
    }

    function drop(account, id) {
        account.tokens = account.tokens.filter((token) => token.id !== id);
        tokenIndex.delete(id);
    }

    function revokeEvery(account) {
        const count = account.tokens.length;
        for (const token of [...account.tokens]) drop(account, token.id);
        return count;
    }

    // Keeps a replaced token's id and hash until it would have expired.
    function spend(account, entry) {
        drop(account, entry.id);
        account.spent = [...account.spent.filter((token) => token.expires > now()), {id: entry.id, hash: entry.hash, expires: entry.expires}];
        while (account.spent.length > MAX_SPENT) spentIndex.delete(account.spent.shift().id);
        spentIndex.set(entry.id, account);
    }

    // A replaced token shown again, with its real secret.
    function replayed(token) {
        if (typeof token !== "string" || token.length > 128) return null;
        const [id, secret] = token.split(".");
        const account = id && secret ? spentIndex.get(id) : null;
        const entry = account?.spent.find((candidate) => candidate.id === id);
        return entry && crypto.timingSafeEqual(sha256(secret), Buffer.from(entry.hash, "hex")) ? account : null;
    }

    // Logs in with a remember token and hands back its replacement.
    function useToken({token, address = "", hardwareId = ""}) {
        const buckets = [`tokenAddress:${address}`];
        const wait = blockedFor(buckets);
        if (wait > 0) return tooMany(wait);
        const found = find(token);
        const copied = !found && replayed(token);
        if (copied) {
            revokeEvery(copied);
            persist();
            note(buckets);
            return {ok: false, field: "form", code: "expired", expired: true, stolen: true,
                message: "This saved login was already used on another computer, so every saved login of this account was signed out. Please log in."};
        }
        const expired = found && found.entry.expires <= now();
        const elsewhere = found && found.entry.hardwareId && found.entry.hardwareId !== hardwareId;
        if (!found || expired || elsewhere) {
            if (expired || elsewhere) {
                drop(found.account, found.entry.id);
                persist();
            }
            note(buckets);
            return {ok: false, field: "form", code: "expired", expired: true, message: "Your saved login has expired. Please log in again."};
        }
        spend(found.account, found.entry);
        found.account.lastLogin = now();
        const next = issueToken(found.account, hardwareId);
        return {ok: true, account: found.account, ...next};
    }

    // The account a valid token belongs to, without using it up.
    function ownerOf(token) {
        const found = find(token);
        return found && found.entry.expires > now() ? found.account : null;
    }

    // Holding a token is enough to throw it away.
    function revokeToken(token) {
        const found = find(token);
        if (!found) return false;
        drop(found.account, found.entry.id);
        persist();
        return true;
    }

    function revokeAll(name) {
        const account = byName(name);
        if (!account) return 0;
        const count = revokeEvery(account);
        persist();
        return count;
    }

    async function setPassword(name, password) {
        const account = byName(name);
        if (!account) return {ok: false, message: "No such account."};
        const check = checkPassword(password, account.name);
        if (!check.ok) return check;
        account.password = await hashPassword(password);
        revokeEvery(account);
        persist();
        return {ok: true};
    }

    // Moves an account onto another record (console only).
    function assignRecord(name, key) {
        const account = byName(name);
        if (!account) return {ok: false, message: "No such account."};
        const owner = byKey(key);
        if (owner && owner !== account) return {ok: false, message: `That record belongs to ${owner.name}.`};
        account.key = key;
        persist();
        return {ok: true, account};
    }

    function list() {
        return Object.values(state.accounts).map(({name, key, created, lastLogin, tokens}) => ({name, key, created, lastLogin, tokens: tokens.length}));
    }

    return {serverId: state.serverId, checkName, checkPassword, byName, byKey, register, login, issueToken, useToken,
        ownerOf, revokeToken, revokeAll, setPassword, assignRecord, list};
}

module.exports = {createAccounts, checkName, checkPassword, LIMITS, TOKEN_TTL_MS, MAX_TOKENS, SCRYPT};
