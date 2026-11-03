// Entering Lost Heaven: a camera flies over the city behind the login card.
// The server builds the flight (lib/flyover.ts: a centripetal Catmull-Rom
// loop checked against the map's skyline, resampled at constant arc length
// with its bank angle) and this script plays it back, interpolating between
// the samples. After the very first registration the camera stops at the
// player's starting district for a one-time look; returning players go
// straight in. Only the server can turn any of it into a player life.
import { plain, toward, type Point } from './lib/geometry';
import { Page } from './lib/page';
import { fields } from './lib/payload';
import { onLocalSpawn } from './lib/player';
import { events, log, onServer, onStart, onStop } from './lib/runtime';

const FREE_RIDE_FRAMES = ['emeth_1', 'emeth_2', 'emeth_3', 'emeth_4'];
const MISSIONS = new Set(['freeride', 'freeride_extended', 'freekrajina']);
const NAVIGATION_SOUND = '00_kompas.wav';
const HELLO_RETRY_MS = 500;
const CAMERA_RECHECK_MS = 200;
const BEGIN_RETRY_MS = 1_000;
const SPAWN_TIMEOUT_MS = 12_000;
const FLYOVER_FOV = 62;
const LOOK_FOV = 54;
// Values per flight sample: x, y, z, tx, ty, tz, roll.
const SAMPLE_SIZE = 7;

type Phase = 'connecting' | 'auth' | 'entering' | 'look';

/** A start frame of the map, reported to the server so it can place anyone. */
interface Anchor {
    frame: string;
    position: Point;
    direction: Point;
}

/** Where the one-time look stands the character. */
interface LookAnchor {
    position: Point;
    direction: Point;
}

interface Character {
    name: string;
    file: string;
    role: string;
}

/** The server's flight table (lib/flyover.ts). */
interface FlightPath {
    samples: number[];
    step: number;
    speed: number;
}

interface Flight extends FlightPath {
    count: number;
}

interface LoginOpen {
    generation: number;
    mission: string | null;
    serverId: string | null;
    nickname: string;
    /** The account name, when the player logged in on an earlier map. */
    authenticated: string | null;
    fallbackModel: string | null;
    flight: FlightPath | null;
}

interface LoginResult {
    ok: boolean;
    name?: string;
    needsLook?: boolean;
    characters?: Character[];
    anchor?: LookAnchor | null;
    token?: string;
    expires?: number;
    field?: string;
    code?: string;
    message?: string;
}

interface LoginError {
    field: string;
    message: string;
    code: string | null;
}

interface PickerState {
    active: boolean;
    open: boolean;
    phase: Phase;
    generation: number;
    anchors: Anchor[];
    nextHelloAt: number;
    nextBeginAt: number;
    missingStartsReported: boolean;
    serverId: string | null;
    nickname: string;
    name: string | null;
    authenticated: string | null;
    message: string;
    error: LoginError | null;
    pending: boolean;
    pendingSince: number;
    flight: Flight | null;
    flightStart: number;
    characters: Character[];
    fallbackModel: string | null;
    modelIndex: number;
    lookAnchor: LookAnchor | null;
    preview: Frame | null;
    cameraSide: number;
    cameraChangedAt: number;
}

const page = new Page(
    'Login card',
    'selector.html',
    { visible: true, focus: false, zIndex: 90 },
    () => {
        page.focus(true);
        sendToPage();
    },
);

const picker: PickerState = {
    active: false,
    open: false,
    phase: 'connecting',
    generation: 0,
    anchors: [],
    nextHelloAt: 0,
    nextBeginAt: 0,
    missingStartsReported: false,
    serverId: null,
    nickname: '',
    name: null,
    authenticated: null,
    message: 'Finding your way into Lost Heaven…',
    error: null,
    pending: false,
    pendingSince: 0,
    flight: null,
    flightStart: 0,
    characters: [],
    fallbackModel: null,
    modelIndex: 0,
    lookAnchor: null,
    preview: null,
    cameraSide: 0,
    cameraChangedAt: 0,
};

function playTick(): void {
    Sound.play(NAVIGATION_SOUND, { volume: 0.65 });
}

function wait(message: string): void {
    picker.pending = true;
    picker.pendingSince = Date.now();
    picker.message = message;
}

// ---- the flight --------------------------------------------------------

// The server's table: every `step` metres a position, a look target and a
// roll, flat as [x, y, z, tx, ty, tz, roll, ...], closing on itself.
function flightFrom(path: FlightPath | null | undefined): Flight | null {
    if (
        !path ||
        !Array.isArray(path.samples) ||
        path.samples.length < SAMPLE_SIZE * 8 ||
        path.samples.length % SAMPLE_SIZE !== 0
    ) {
        return null;
    }

    if (!path.samples.every(Number.isFinite) || !(path.step > 0) || !(path.speed > 0)) {
        return null;
    }

    return {
        samples: path.samples,
        count: path.samples.length / SAMPLE_SIZE,
        step: path.step,
        speed: path.speed,
    };
}

function uniform(p0: number, p1: number, p2: number, p3: number, t: number): number {
    return (
        0.5 *
        (2 * p1 +
            (p2 - p0) * t +
            (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
            (3 * p1 - p0 - 3 * p2 + p3) * t * t * t)
    );
}

// The camera at `distance` metres along the loop. The samples are evenly
// spaced in arc length, so a uniform Catmull-Rom between them keeps the
// speed constant and the motion smooth.
function flightAt(
    flight: Flight,
    distance: number,
): { position: Point; direction: Point; roll: number } {
    const { samples, count, step } = flight;
    const length = count * step;
    const along = (((distance % length) + length) % length) / step;
    const i = Math.floor(along);
    const t = along - i;
    const value = (field: number): number =>
        uniform(
            samples[((i - 1 + count) % count) * SAMPLE_SIZE + field],
            samples[i * SAMPLE_SIZE + field],
            samples[((i + 1) % count) * SAMPLE_SIZE + field],
            samples[((i + 2) % count) * SAMPLE_SIZE + field],
            t,
        );
    const position = { x: value(0), y: value(1), z: value(2) };
    const target = { x: value(3), y: value(4), z: value(5) };

    return { position, direction: toward(position, target), roll: value(6) };
}

function flyFrame(): void {
    if (!picker.flight) {
        return;
    }

    const pose = flightAt(
        picker.flight,
        ((Date.now() - picker.flightStart) / 1000) * picker.flight.speed,
    );

    Camera.lock(pose.position, pose.direction, pose.roll);
}

function startFlight(path: FlightPath | null | undefined): void {
    picker.flight = flightFrom(path);

    if (!picker.flight) {
        return;
    }

    const { count, step, speed } = picker.flight;

    // Every session begins somewhere else along the loop.
    picker.flightStart = Date.now() - ((Math.random() * count * step) / speed) * 1000;
    Camera.setFov(FLYOVER_FOV);
}

// ---- the page ----------------------------------------------------------

function sendToPage(): void {
    const model = picker.characters[picker.modelIndex];

    page.emit('login:state', {
        phase: picker.phase,
        open: picker.open,
        serverId: picker.serverId,
        nickname: picker.nickname,
        name: picker.name,
        authenticated: picker.authenticated,
        pending: picker.pending,
        message: picker.message,
        error: picker.error,
        model: model ? { name: model.name, role: model.role } : null,
        modelIndex: picker.modelIndex,
        modelCount: picker.characters.length,
    });
}

function bindPage(): void {
    page.on('login:submit', (payload) => {
        if (picker.phase !== 'auth' || picker.pending) {
            return;
        }

        const request = fields(payload);
        const mode = request.mode === 'register' ? 'register' : 'login';

        if (typeof request.name !== 'string' || typeof request.password !== 'string') {
            return;
        }

        picker.error = null;
        wait(mode === 'register' ? 'Writing your name in the book…' : 'Checking the book…');
        sendToPage();
        events.emitServer('fr:login:submit', {
            generation: picker.generation,
            mode,
            name: request.name,
            password: request.password,
            remember: Boolean(request.remember),
            logoutOthers: request.logoutOthers === true,
        });
    });

    page.on('login:token', (payload) => {
        const token = fields(payload).token;

        if (picker.phase !== 'auth' || picker.pending || typeof token !== 'string') {
            return;
        }

        picker.error = null;
        wait('Welcome back. The city is opening…');
        sendToPage();
        events.emitServer('fr:login:token', { generation: picker.generation, token });
    });

    // Log out of a remembered account: the server throws the token away.
    page.on('login:forget', (payload) => {
        const token = fields(payload).token;

        if (typeof token === 'string') {
            events.emitServer('fr:login:forget', { token });
        }
    });

    page.on('login:look:move', (payload) => moveModel(fields(payload).step === -1 ? -1 : 1));
    page.on('login:look:camera', rotateCamera);
    page.on('login:look:confirm', confirmLook);
}

// ---- the one-time look -------------------------------------------------

function freePreview(): void {
    picker.preview?.destroy();
    picker.preview = null;
}

function cameraFor(anchor: LookAnchor, side: number): void {
    const { position, direction } = anchor;
    const length = Math.hypot(direction.x, direction.z) || 1;
    const forward = { x: direction.x / length, z: direction.z / length };
    const directions = [
        forward,
        { x: -forward.z, z: forward.x },
        { x: -forward.x, z: -forward.z },
        { x: forward.z, z: -forward.x },
    ];
    const away = directions[side];
    const eye = { x: position.x + away.x * 4.0, y: position.y + 2.1, z: position.z + away.z * 4.0 };
    const subject = { x: position.x, y: position.y + 1.25, z: position.z };

    Camera.lock(eye, toward(eye, subject));
    Camera.setFov(LOOK_FOV);
    picker.cameraChangedAt = Date.now();
}

function showLook(): void {
    const anchor = picker.lookAnchor;

    if (!anchor) {
        return;
    }

    freePreview();

    // Native humans require more than a file that merely opens. The scripting
    // probe uses the same full-skeleton check as the player's model swap.
    let attempts = picker.characters.length;

    while (attempts-- > 0) {
        const model = picker.characters[picker.modelIndex];

        picker.preview = Scene.createHumanFrame(model.file, anchor.position, anchor.direction);

        if (picker.preview !== null) {
            break;
        }

        picker.modelIndex = (picker.modelIndex + 1) % picker.characters.length;
    }

    if (picker.preview === null) {
        skipLook();

        return;
    }

    picker.cameraSide = 0;
    cameraFor(anchor, picker.cameraSide);
    picker.message = 'How will Lost Heaven first see you? What you become is up to you.';
    sendToPage();
}

function moveModel(step: number): void {
    if (picker.phase !== 'look' || picker.pending || picker.characters.length < 2) {
        return;
    }

    picker.modelIndex =
        (picker.modelIndex + step + picker.characters.length) % picker.characters.length;

    playTick();
    showLook();
}

function rotateCamera(): void {
    if (picker.phase !== 'look' || picker.pending || !picker.lookAnchor) {
        return;
    }

    picker.cameraSide = (picker.cameraSide + 1) % 4;
    cameraFor(picker.lookAnchor, picker.cameraSide);
    playTick();
    sendToPage();
}

function confirmLook(): void {
    if (picker.phase !== 'look' || picker.pending || picker.preview === null) {
        return;
    }

    wait('Your story begins…');
    playTick();
    sendToPage();
    events.emitServer('fr:login:look', {
        generation: picker.generation,
        model: picker.characters[picker.modelIndex].file,
    });
}

// Without a look this client can show, the player starts as the server's
// first character instead of being stuck here.
function skipLook(): void {
    freePreview();
    picker.phase = 'entering';
    wait('Your story begins…');
    sendToPage();

    if (picker.fallbackModel) {
        events.emitServer('fr:login:look', {
            generation: picker.generation,
            model: picker.fallbackModel,
        });
    }
}

function isFinitePoint(point: Point | null | undefined): boolean {
    return Boolean(point) && [point?.x, point?.y, point?.z].every(Number.isFinite);
}

function beginLook(result: LoginResult): void {
    const anchor = result.anchor;
    const valid = anchor && isFinitePoint(anchor.position) && isFinitePoint(anchor.direction);
    const offered = Array.isArray(result.characters)
        ? result.characters.filter((model) => typeof model?.file === 'string')
        : [];

    picker.fallbackModel = offered[0]?.file ?? picker.fallbackModel;
    picker.characters = offered.filter((model) => Scene.canUseHumanModel(model.file));

    if (!valid || !picker.characters.length) {
        skipLook();

        return;
    }

    picker.phase = 'look';
    picker.flight = null;
    picker.lookAnchor = anchor;
    picker.modelIndex = 0;
    showLook();
}

// ---- lifecycle ---------------------------------------------------------

function startFrames(): Anchor[] {
    const anchors: Anchor[] = [];

    for (const frame of FREE_RIDE_FRAMES) {
        const worldFrame = World.findWorldFrame(frame);
        const position = worldFrame?.getWorldPosition();
        const direction = worldFrame?.getWorldDirection();

        if (position && direction) {
            anchors.push({ frame, position: plain(position), direction: plain(direction) });
        }
    }

    return anchors;
}

function begin(): void {
    if (picker.active || !MISSIONS.has(World.getMission() ?? '') || LocalPlayer?.spawned) {
        return;
    }

    const anchors = startFrames();

    if (!anchors.some((anchor) => anchor.frame === FREE_RIDE_FRAMES[0]) || anchors.length < 2) {
        picker.nextBeginAt = Date.now() + BEGIN_RETRY_MS;

        if (!picker.missingStartsReported) {
            picker.missingStartsReported = true;
            log.warn('Waiting for Free Ride start frames');
        }

        return;
    }

    Object.assign(picker, {
        active: true,
        open: false,
        phase: 'connecting',
        generation: World.getMissionGeneration(),
        anchors,
        nextHelloAt: 0,
        missingStartsReported: false,
        authenticated: null,
        error: null,
        pending: false,
        flight: null,
        characters: [],
        lookAnchor: null,
        message: 'Finding your way into Lost Heaven…',
    } satisfies Partial<PickerState>);

    if (!page.open()) {
        return;
    }

    bindPage();
    Hud.hideWatch();
    Hud.hideScore();
    // Until the server sends the flight, hold the map on a start frame.
    cameraFor(anchors.find((anchor) => anchor.frame === 'emeth_3') ?? anchors[0], 0);
}

function end(): void {
    if (!picker.active) {
        return;
    }

    freePreview();
    page.close();
    picker.active = false;
    picker.open = false;
    picker.flight = null;
    picker.characters = [];
    picker.lookAnchor = null;
    Camera.unlock();
}

events.on('missionReady', begin);
onStart(() => {
    if (World.isReady()) {
        begin();
    }
});

events.on('missionUnload', end);
onStop(end);
onLocalSpawn(end);

onServer<LoginOpen>('fr:login:open', (state) => {
    if (
        !picker.active ||
        picker.open ||
        state.generation !== picker.generation ||
        state.mission !== World.getMission()
    ) {
        return;
    }

    picker.open = true;
    picker.serverId = typeof state.serverId === 'string' ? state.serverId : null;
    picker.nickname = typeof state.nickname === 'string' ? state.nickname : '';
    picker.authenticated = typeof state.authenticated === 'string' ? state.authenticated : null;
    picker.fallbackModel = typeof state.fallbackModel === 'string' ? state.fallbackModel : null;
    startFlight(state.flight);

    if (picker.authenticated) {
        // Already logged in (a new map): the server sends the next step.
        picker.phase = 'entering';
        picker.name = picker.authenticated;
        picker.message = `Welcome back, ${picker.authenticated}. The city is opening…`;
    } else {
        picker.phase = 'auth';
        picker.message = '';
    }

    sendToPage();
});

onServer<LoginResult>('fr:login:result', (result) => {
    if (!picker.active) {
        return;
    }

    picker.pending = false;

    if (!result.ok) {
        picker.error = {
            field: typeof result.field === 'string' ? result.field : 'form',
            message:
                typeof result.message === 'string'
                    ? result.message
                    : 'That did not work. Try again.',
            code: typeof result.code === 'string' ? result.code : null,
        };

        picker.message = '';
        sendToPage();

        return;
    }

    picker.error = null;
    picker.name = typeof result.name === 'string' ? result.name : picker.name;
    picker.authenticated = picker.name;
    // The page keeps the replacement token for the next start.
    page.emit(
        'login:remember',
        typeof result.token === 'string' && picker.serverId
            ? {
                  serverId: picker.serverId,
                  name: picker.name,
                  token: result.token,
                  expires: result.expires,
              }
            : { serverId: picker.serverId, name: picker.name, token: null },
    );

    if (result.needsLook) {
        beginLook(result);
    } else {
        picker.phase = 'entering';
        wait(`Welcome back, ${picker.name}. The city is opening…`);
        sendToPage();
    }
});

events.on('fr:login:accepted', () => {
    if (!picker.active) {
        return;
    }

    picker.phase = 'entering';
    wait('The city is opening. One moment…');
    sendToPage();
});

onServer<{ message?: string }>('fr:login:error', (error) => {
    if (!picker.active) {
        return;
    }

    picker.pending = false;
    picker.message =
        typeof error.message === 'string'
            ? error.message
            : 'That entrance is unavailable. Try again.';

    sendToPage();
});

// Until the server answers, and after a new map while logged in, the
// client keeps reporting its start frames; the server needs them to
// place anyone.
function sayHello(now: number): void {
    if ((picker.open && picker.phase !== 'entering') || now < picker.nextHelloAt) {
        return;
    }

    events.emitServer('fr:login:hello', {
        generation: picker.generation,
        anchors: picker.anchors,
        opened: picker.open,
    });

    picker.nextHelloAt = now + (picker.open ? HELLO_RETRY_MS * 4 : HELLO_RETRY_MS);
}

// The look's camera starts in front of the character and walks round until
// it can see their head.
function findLookSide(now: number): void {
    const anchor = picker.lookAnchor;

    if (!anchor || picker.cameraSide >= 3 || now - picker.cameraChangedAt < CAMERA_RECHECK_MS) {
        return;
    }

    const head = { x: anchor.position.x, y: anchor.position.y + 1.25, z: anchor.position.z };

    if (!Scene.projectWorld(head, true)?.visible) {
        picker.cameraSide += 1;
        cameraFor(anchor, picker.cameraSide);
    } else {
        picker.cameraChangedAt = Number.POSITIVE_INFINITY;
    }
}

events.on('render', () => {
    const now = Date.now();

    if (!picker.active) {
        if (
            World.isReady() &&
            MISSIONS.has(World.getMission() ?? '') &&
            !LocalPlayer?.spawned &&
            now >= picker.nextBeginAt
        ) {
            begin();
        }

        return;
    }

    if (LocalPlayer?.spawned) {
        end();

        return;
    }

    sayHello(now);

    if (picker.pending && now - picker.pendingSince > SPAWN_TIMEOUT_MS) {
        picker.pending = false;
        picker.pendingSince = Number.POSITIVE_INFINITY;
        picker.message =
            picker.phase === 'entering'
                ? 'Your place is ready. Waiting for your player…'
                : 'The city did not answer. Try again.';

        sendToPage();
    }

    if (picker.phase === 'look') {
        findLookSide(now);

        return;
    }

    flyFrame();
});
