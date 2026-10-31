// Logging in. A joining player logs in or registers (lib/accounts.ts) while
// a camera flies over the map (lib/flyover.ts). Logging in attaches the
// account's player record and puts them straight on the street
// (selector/entry.ts), or first has a new player pick a look (selector/look.ts).
import * as profiles from '../lib/profiles';
import * as maps from '../lib/maps';
import * as store from '../lib/store';
import * as flyover from '../lib/flyover';
import * as log from '../lib/log';
import type { Account, Refusal, TokenResult } from '../lib/accounts';
import { inFreeRide, emit, onClient, connected } from '../lib/util';
import { events, every, onMissionChange, onStart, onStop } from '../lib/lifecycle';
import { PlayerMap, PlayerSet } from '../lib/players';
import { Cooldown } from '../lib/cooldowns';
import { announceLogin } from '../lib/login';
import { fields } from '../lib/payload';
import { CHARACTERS } from './characters';
import { accounts, flushAccounts } from './accounts';
import { inTown, makeGuest, sessions } from './sessions';
import {
    clearSelections,
    enter,
    isolatePreview,
    isolateWaitingPlayers,
    leavePreviewWorlds,
    saveResume,
    takeAnchors,
} from './entry';
import { lookOffer, type LookOffer } from './look';

// The card carries the flight (about 50 KB); a client that has not said it
// got one is sent another after this long.
const OPEN_RESEND_MS = 4000;
const REQUEST_INTERVAL_MS = 1000;
// Logins survive a restart of this resource for this long.
const HANDOFF_MS = 10 * 60_000;
// Often enough that a dropped connection loses only a few steps. Not on
// disconnect: lib/life.ts has already ended the player's activity by then.
const RESUME_SAVE_MS = 5000;

/** A login the client is told about. */
type SignedIn = { ok: true; name: string; needsLook: boolean } & Partial<LookOffer>;

type LoginReply =
    | Refusal
    | SignedIn
    | (SignedIn & { token: string; expires: number })
    | { ok: false; field: 'form'; code: 'in-use'; message: string };

/** The logins handed over from before a resource restart. */
interface LoginHandoff {
    at: number;
    sessions?: { id: number; nickname: string; hardwareId: string; account: string }[];
}

const IN_USE = {
    ok: false,
    field: 'form',
    code: 'in-use',
    message: 'That account is already in town.',
} as const;

// A ticket for the one login request in flight.
const inFlight = new PlayerMap<object>();
const requests = new Cooldown(REQUEST_INTERVAL_MS);
// The nickname each player joined with, which only fills in the form.
const joinNames = new PlayerMap<string>();
// The mission generation and time of the last login card sent.
const opened = new PlayerMap<{ generation: number; at: number }>();
const reportedReady = new PlayerSet();

// After a password, a token or a fresh registration.
function signIn(
    player: Player,
    account: Account,
    extra: { registered?: boolean; claimed?: boolean } = {},
): SignedIn | typeof IN_USE {
    if (inTown(player, account)) {
        return IN_USE;
    }

    // A guest who joined under this name gives it up to its owner.
    const holder = World.getPlayers().find(
        (candidate) =>
            candidate.id !== player.id &&
            candidate.nickname.toLowerCase() === account.name.toLowerCase(),
    );

    if (holder) {
        holder.setNickname(`Guest ${holder.id}`);
    }

    player.setNickname(account.name);

    const { record, returning } = profiles.attach(player, account.key);
    const needsLook = !record.model;

    sessions.set(player.id, { account: account.name.toLowerCase(), needsLook });
    announceLogin(player, { returning, account: account.name, claimed: Boolean(extra.claimed) });
    log.info(
        `${player.nickname} logged in` +
            (extra.registered
                ? ` (new account${extra.claimed ? ', claimed an earlier record)' : ')'}`
                : ''),
    );

    return {
        ok: true,
        name: player.nickname,
        needsLook,
        ...(needsLook ? lookOffer(player) : {}),
    };
}

function finish(player: Player, result: LoginReply): void {
    emit(player, 'fr:login:result', result);

    if (result.ok && !result.needsLook) {
        enter(player);
    }
}

// One request at a time per player, at most one a second; the answer is
// dropped if the player left or another request took its place.
async function guarded<T>(player: Player, work: () => Promise<T>): Promise<T | Refusal | null> {
    if (inFlight.has(player.id)) {
        return null;
    }

    if (requests.take(player) > 0) {
        return { ok: false, field: 'form', message: 'Not so fast. Try again in a second.' };
    }

    const ticket = {};

    inFlight.set(player.id, ticket);
    let result: T | Refusal;

    try {
        result = await work();
    } catch (caught) {
        log.error(`Login for ${player.nickname} failed: ${log.describe(caught, true)}`);
        result = { ok: false, field: 'form', message: 'The city could not check that. Try again.' };
    } finally {
        if (inFlight.get(player.id) === ticket) {
            inFlight.delete(player.id);
        }
    }

    if (connected(player) && !sessions.has(player.id)) {
        return result;
    }

    return null;
}

function canLogIn(player: Player, request: unknown): boolean {
    return (
        inFreeRide() &&
        typeof request === 'object' &&
        request !== null &&
        !sessions.has(player.id) &&
        !player.spawned
    );
}

onClient('fr:login:submit', async (player, payload: unknown) => {
    const request = fields(payload);
    const { name, password, mode } = request;

    if (!canLogIn(player, payload) || typeof name !== 'string' || typeof password !== 'string') {
        return;
    }

    const address = player.ip;
    const result = await guarded(player, async () => {
        if (mode === 'register') {
            // Only a record from this same machine; see lib/accounts.ts.
            const claim = player.hardwareId
                ? {
                      key: profiles.legacyKey(name.trim().replace(/\s+/g, ' '), player.hardwareId),
                      exists: profiles.exists,
                  }
                : null;

            return accounts.register({ name, password, address, claim });
        }

        return accounts.login({
            name,
            password,
            address,
            logoutOthers: request.logoutOthers === true,
        });
    });

    if (!result) {
        return;
    }

    if (!result.ok) {
        finish(player, result);

        return;
    }

    const signed = signIn(player, result.account, {
        registered: mode === 'register',
        claimed: 'claimed' in result && result.claimed === true,
    });

    finish(
        player,
        signed.ok && request.remember
            ? { ...signed, ...accounts.issueToken(result.account, player.hardwareId) }
            : signed,
    );
});

onClient('fr:login:token', async (player, payload: unknown) => {
    const { token } = fields(payload);

    if (!canLogIn(player, payload) || typeof token !== 'string') {
        return;
    }

    // An account already in town leaves the token unspent for later.
    const result = await guarded(player, async (): Promise<TokenResult | typeof IN_USE> => {
        const owner = accounts.ownerOf(token);

        return owner && inTown(player, owner)
            ? IN_USE
            : accounts.useToken({ token, address: player.ip, hardwareId: player.hardwareId });
    });

    if (!result) {
        return;
    }

    if (!result.ok) {
        finish(player, result);

        return;
    }

    const signed = signIn(player, result.account);

    if (!signed.ok) {
        accounts.revokeToken(result.token);
    }

    finish(
        player,
        signed.ok ? { ...signed, token: result.token, expires: result.expires } : signed,
    );
});

onClient('fr:login:forget', (_player, payload: unknown) => {
    const { token } = fields(payload);

    if (typeof token === 'string') {
        accounts.revokeToken(token);
    }
});

// The client's hello: the districts it found, and whether it shows the card.
onClient('fr:login:hello', (player, payload: unknown) => {
    const report = fields(payload);
    const anchors = Array.isArray(report.anchors) ? (report.anchors as unknown[]) : null;

    isolatePreview(player);

    if (!reportedReady.has(player.id)) {
        reportedReady.add(player.id);
        log.info(
            `Login report from ${player.nickname}: frames ${anchors?.map((anchor) => fields(anchor).frame).join(',')}`,
        );
    }

    const map = maps.current();
    const generation = World.getMissionGeneration();

    if (
        !map ||
        !World.isReady() ||
        player.spawned ||
        report.generation !== generation ||
        !anchors ||
        !takeAnchors(player, generation, anchors)
    ) {
        return;
    }

    const session = sessions.get(player.id);

    if (!session) {
        makeGuest(player);
    }

    // Once per map, and again only if the client still has none after a
    // while (it started over, or the card was lost).
    const last = opened.get(player.id);
    const due =
        !last ||
        last.generation !== generation ||
        (report.opened !== true && Date.now() - last.at > OPEN_RESEND_MS);

    if (due) {
        opened.set(player.id, { generation, at: Date.now() });
        emit(player, 'fr:login:open', {
            generation,
            mission: World.getMission(),
            serverId: accounts.serverId,
            nickname: joinNames.get(player.id) ?? '',
            authenticated: session ? player.nickname : null,
            flight: flyover.flightFor(map.id),
            fallbackModel: CHARACTERS[0].file,
        });

        // Logged in already (a new map): the next step follows straight away.
        if (session?.needsLook) {
            emit(player, 'fr:login:result', {
                ok: true,
                name: player.nickname,
                needsLook: true,
                ...lookOffer(player),
            });
        }
    }

    if (session && !session.needsLook) {
        enter(player);
    }
});

events.on('playerConnect', (player) => {
    joinNames.set(player.id, player.nickname);

    if (inFreeRide()) {
        makeGuest(player);
    }

    isolatePreview(player);
});

events.on('playerMissionReady', isolatePreview);

onMissionChange(() => {
    clearSelections();

    if (inFreeRide()) {
        isolateWaitingPlayers();
    } else {
        leavePreviewWorlds();
    }
});

// A restart of this resource keeps the players connected and in the city;
// their logins are handed over through the database.
function restoreSessions(): void {
    const handoff = store.load<LoginHandoff>('login_sessions');

    store.save('login_sessions', { at: 0, sessions: [] } satisfies LoginHandoff);

    if (!handoff || Date.now() - handoff.at > HANDOFF_MS) {
        return;
    }

    for (const saved of handoff.sessions ?? []) {
        const player = World.getPlayers().find(
            (candidate) =>
                candidate.id === saved.id &&
                candidate.hardwareId === saved.hardwareId &&
                candidate.nickname === saved.nickname,
        );
        const account = player && accounts.byName(saved.account);

        if (!player || !account || sessions.has(player.id)) {
            continue;
        }

        const { record, returning } = profiles.attach(player, account.key);

        sessions.set(player.id, {
            account: account.name.toLowerCase(),
            needsLook: !record.model && !player.spawned,
        });

        announceLogin(player, { returning, account: account.name, restored: true });
    }
}

// Whoever the handoff did not cover goes back to the login card.
function returnToLogin(): void {
    if (!inFreeRide()) {
        return;
    }

    for (const player of World.getPlayers()) {
        if (sessions.has(player.id)) {
            continue;
        }

        makeGuest(player);

        if (player.spawned) {
            player.despawn();
        }
    }
}

onStart(() => {
    restoreSessions();
    returnToLogin();
    isolateWaitingPlayers();
});

every(RESUME_SAVE_MS, () => World.getPlayers().forEach(saveResume));

onStop(() => {
    flushAccounts();

    for (const player of World.getPlayers()) {
        saveResume(player);
    }

    store.save('login_sessions', {
        at: Date.now(),
        sessions: World.getPlayers().flatMap((player) => {
            const session = sessions.get(player.id);

            return session
                ? [
                      {
                          id: player.id,
                          nickname: player.nickname,
                          hardwareId: player.hardwareId,
                          account: session.account,
                      },
                  ]
                : [];
        }),
    } satisfies LoginHandoff);

    clearSelections();
    leavePreviewWorlds();
});
