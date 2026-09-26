import { mkdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { randomInt } from 'node:crypto';
import { Accounts } from './accounts';
import { AdminCommands } from './admin';
import { adminText } from './messages';
import type { Session } from './session';
import { hashPassword, verifyPassword } from './password';
import { RateLimit } from './rate-limit';
import { loadCameraPaths, saveCameraPaths } from './camera-paths';
import { isCameraPaths } from '../shared/camera';
import {
    credentialsError,
    EVENT,
    isModel,
    isPoint,
    isRecord,
    isRememberToken,
    MODELS,
    RESOURCE,
    type AuthRequest,
    type ErrorCode,
    type SavedPlace,
    type SessionState,
} from '../shared/protocol';

const events = Events as EventBus;

// Kept outside the downloadable resource and preserved by every build.
const data = resolve(__dirname, '../../data');

mkdirSync(data, { recursive: true });
const accounts = new Accounts(resolve(data, 'accounts.sqlite'));
const cameraFile = resolve(data, 'camera-paths.json');
const sessions = new Map<number, Session>();
const online = new Set<number>();
const limits = new RateLimit();
const admin = new AdminCommands(accounts, sessions, saveSafely);
let hashing = 0;
let stopping = false;
let nextPrivateWorld = 0x40000000;

function canRecord(player: Player): boolean {
    return (
        process.env.LHRP_CAMERA_EDITOR === '1' &&
        ['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(player.getIP())
    );
}

function sendCameraPaths(player: Player): void {
    const mission = World.getMission();

    if (!mission) {
        return;
    }

    try {
        player.emit(EVENT.cameraPaths, JSON.stringify(loadCameraPaths(cameraFile, mission)));
    } catch (error) {
        process.stderr.write(`[${RESOURCE}] Could not load camera paths: ${error}\n`);
    }
}

function send(session: Session, error?: ErrorCode): void {
    const state: SessionState = {
        generation: World.getMissionGeneration(),
        phase: session.entered ? 'playing' : session.account ? 'spawning' : 'auth',
        username: session.account?.username,
        returning: Boolean(session.account?.place),
        error,
        cameraEditor: canRecord(session.player),
        serverId: accounts.serverId,
        remembered: session.remembered,
        forgetRemembered: session.forgetRemembered,
    };

    session.player.emit(EVENT.state, JSON.stringify(state));
}

function connect(player: Player): Session {
    const existing = sessions.get(player.id);

    if (existing) {
        return existing;
    }

    const session: Session = {
        player,
        locale: 'en',
        busy: false,
        entered: false,
        respawnAt: 0,
        rememberUntil: 0,
    };

    sessions.set(player.id, session);

    // Also makes a resource restart require authentication again.
    if (player.spawned) {
        player.despawn();
    }

    player.setVirtualWorld(nextPrivateWorld++);

    return session;
}

function save(session: Session): void {
    const { player, account } = session;

    if (!account || !session.entered || !player.spawned || !player.alive) {
        return;
    }

    const mission = World.getMission();
    const position = player.getVehicle()?.position ?? player.position;
    const rotation = player.rotation;
    const heading = Math.atan2(
        2 * (rotation.w * rotation.y + rotation.x * rotation.z),
        1 - 2 * (rotation.y * rotation.y + rotation.x * rotation.x),
    );

    if (!mission || !isPoint(position) || !Number.isFinite(heading)) {
        return;
    }

    const place: SavedPlace = {
        mission,
        position: { x: position.x, y: position.y, z: position.z },
        heading,
    };

    accounts.save(account.id, player.model, session.locale, place);
    account.place = place;
    account.model = player.model;
}

function saveSafely(session: Session): void {
    try {
        save(session);
    } catch (error) {
        process.stderr.write(
            `[${RESOURCE}] Could not save account ${session.account?.id}: ${error}\n`,
        );
    }
}

function enter(session: Session): void {
    const { player, account } = session;

    if (
        !account ||
        session.entered ||
        Date.now() < session.rememberUntil ||
        !World.isReady() ||
        !player.getSuggestedSpawn()
    ) {
        return;
    }

    const ban = accounts.getBan(account.id);

    if (ban) {
        player.kick(adminText(session.locale, 'banned', ban));

        return;
    }

    const saved = account.place;
    const place =
        saved?.mission === World.getMission() && isPoint(saved.position)
            ? saved
            : { position: player.getSuggestedSpawn()!, heading: 0 };
    const model = isModel(account.model) ? account.model : MODELS[randomInt(MODELS.length)].file;

    if (!player.setModel(model) || !player.spawn(place.position, place.heading)) {
        return;
    }

    player.setVirtualWorld(0);
    player.setNickname(account.username);
    session.entered = true;
    session.remembered = undefined;
    saveSafely(session);
    send(session);
}

async function authenticate(player: Player, payload: unknown): Promise<void> {
    const session = sessions.get(player.id);

    if (
        !session ||
        stopping ||
        !isRecord(payload) ||
        payload.generation !== World.getMissionGeneration()
    ) {
        return;
    }

    if (session.account) {
        send(session);

        return;
    }

    if (session.busy) {
        return;
    }

    if (!limits.take(player.getIP()) || hashing >= 2) {
        send(session, 'rateLimited');

        return;
    }

    const error = credentialsError(payload);

    if (error) {
        send(session, error);

        return;
    }

    const request = payload as unknown as AuthRequest;

    session.busy = true;
    hashing++;

    try {
        let account =
            request.mode === 'remembered'
                ? accounts.recall(request.token)
                : accounts.find(request.username);
        const encoded =
            request.mode === 'register'
                ? await hashPassword(request.password)
                : request.mode === 'remembered'
                  ? Boolean(account)
                  : await verifyPassword(request.password, account?.passwordHash);

        // An async hash must never authenticate a disconnected or replacement session.
        if (stopping || sessions.get(player.id) !== session) {
            return;
        }

        if (request.mode === 'register') {
            if (account) {
                send(session, 'usernameTaken');

                return;
            }

            account = accounts.create(request.username, encoded as string, request.locale);

            if (!account) {
                send(session, 'usernameTaken');

                return;
            }
        } else if (!encoded || !account) {
            send(
                session,
                request.mode === 'remembered' ? 'rememberedExpired' : 'invalidCredentials',
            );

            return;
        }

        if (accounts.getBan(account.id)) {
            send(session, 'banned');

            return;
        }

        if (online.has(account.id)) {
            send(session, 'alreadyOnline');

            return;
        }

        if (request.remember && request.mode !== 'remembered') {
            session.remembered = accounts.remember(account);
            // Give CEF time to persist the token before spawning destroys the login view.
            session.rememberUntil = Date.now() + 3000;
        }

        online.add(account.id);
        session.rememberToken =
            request.mode === 'remembered' ? request.token : session.remembered?.token;

        session.forgetRemembered = false;
        session.account = account;
        session.locale = request.locale;
        send(session);
        enter(session);
    } catch (error) {
        process.stderr.write(`[${RESOURCE}] Account operation failed: ${error}\n`);

        if (!stopping && sessions.get(player.id) === session) {
            send(session, 'unavailable');
        }
    } finally {
        session.busy = false;
        hashing--;
    }
}

events.on('playerConnect', connect);
events.onClient(EVENT.ready, (sender, payload) => {
    const player = sender as Player; // Framework supplies the authenticated transport sender.

    if (!isRecord(payload) || payload.generation !== World.getMissionGeneration()) {
        return;
    }

    const session = connect(player);

    if (!session.busy) {
        send(session);
        sendCameraPaths(player);
    }
});

events.onClient(EVENT.cameraSave, (sender, payload) => {
    const player = sender as Player;

    if (!canRecord(player) || !isCameraPaths(payload) || payload.mission !== World.getMission()) {
        return;
    }

    try {
        saveCameraPaths(cameraFile, payload);
        player.emit(EVENT.cameraSaved, JSON.stringify({ ok: true }));
        sendCameraPaths(player);
    } catch (error) {
        process.stderr.write(`[${RESOURCE}] Could not save camera paths: ${error}\n`);
        player.emit(EVENT.cameraSaved, JSON.stringify({ ok: false }));
    }
});

events.onClient(EVENT.auth, (sender, payload) => authenticate(sender as Player, payload));
events.onClient(EVENT.rememberedReady, (sender) => {
    const session = sessions.get((sender as Player).id);

    if (session?.remembered) {
        session.rememberUntil = 0;
        enter(session);
    }
});

events.onClient(EVENT.forget, (_sender, payload) => {
    if (isRecord(payload) && isRememberToken(payload.token)) {
        accounts.forget(payload.token);
    }
});

events.on('consoleCommand', (command, args) => admin.handleConsole(command, args));

events.on('playerCommand', (player, command, args) => {
    if (admin.handle(player, command, args)) {
        return;
    }

    const session = sessions.get(player.id);

    if (command.toLowerCase() !== 'logout' || !session?.account) {
        return;
    }

    saveSafely(session);
    admin.release(player);

    if (session.rememberToken) {
        accounts.forget(session.rememberToken);
    }

    online.delete(session.account.id);
    session.account = undefined;
    session.entered = false;
    session.respawnAt = 0;
    session.rememberUntil = 0;
    session.remembered = undefined;
    session.rememberToken = undefined;
    session.forgetRemembered = true;
    player.despawn();
    player.setVirtualWorld(nextPrivateWorld++);
    send(session);
});

events.on('playerDisconnect', (player) => {
    const session = sessions.get(player.id);

    if (!session) {
        return;
    }

    saveSafely(session); // Native handle is still valid during playerDisconnect.
    admin.release(player);

    if (session.account) {
        online.delete(session.account.id);
    }

    sessions.delete(player.id);
});

events.on('playerDeath', (player) => {
    const session = sessions.get(player.id);

    if (session?.entered) {
        session.respawnAt = Date.now() + 5000;
    }
});

events.on('missionChange', () => {
    admin.reset();
    // The native world has already reset: keep the last checkpoint, never save the reset pose.
    for (const session of sessions.values()) {
        session.entered = false;
        session.respawnAt = 0;
        session.player.setVirtualWorld(nextPrivateWorld++);
    }
});

events.on('resourceStart', (name) => {
    if (name === RESOURCE) {
        for (const player of World.getPlayers()) {
            connect(player);
        }
    }
});

const spawnTimer = setInterval(() => {
    admin.update();
    for (const session of sessions.values()) {
        if (session.respawnAt && Date.now() >= session.respawnAt && World.isReady()) {
            const position = session.player.getSuggestedSpawn();

            if (position && session.player.respawn(position)) {
                session.respawnAt = 0;
            }
        }

        enter(session);
    }
}, 500);
const saveTimer = setInterval(() => {
    for (const session of sessions.values()) {
        saveSafely(session);
    }

    limits.prune();
}, 30_000);

events.on('resourceStop', (name) => {
    if (name !== RESOURCE) {
        return;
    }

    stopping = true;
    clearInterval(spawnTimer);
    clearInterval(saveTimer);
    for (const session of sessions.values()) {
        saveSafely(session);
    }

    admin.reset();
    sessions.clear();
    online.clear();
    accounts.close();
});
