import './properties';
import './positions';
import { Cinematic } from './cinematic';
import { CameraRecorder } from './recorder';
import { isCameraPaths } from '../shared/camera';
import loginCamera from '../shared/login-camera.json';
import { EVENT, isRecord, RESOURCE, type ViewState } from '../shared/protocol';

const events = Events as EventBus;

let view: number | null = null;
let camera: Cinematic | null = null;
let recorder: CameraRecorder | null = null;
let previousFrame = Date.now();
let pageReady = false;
let nextHello = 0;
let pendingSince = 0;
let previousChat = true;
let state: ViewState = { generation: 0, phase: 'auth', connected: false, pending: false };

function publish(): void {
    if (view !== null && pageReady) {
        Web.emit(view, EVENT.state, state);
    }
}

function close(): void {
    if (view === null) {
        return;
    }

    Web.destroyView(view);
    view = null;
    camera?.stop();
    camera = null;
    recorder = null;
    pageReady = false;
    Chat.setUIVisible(previousChat);
}

function open(): void {
    if (view !== null || !World.isReady() || LocalPlayer?.spawned) {
        return;
    }

    state = {
        generation: World.getMissionGeneration(),
        phase: 'auth',
        connected: false,
        pending: false,
    };

    view = Web.createView(`fw://resources/${RESOURCE}/ui/index.html`, {
        visible: true,
        focus: true,
        zIndex: 100,
    });

    previousChat = Chat.isUIVisible();
    Chat.close();
    Chat.setUIVisible(false);
    Hud.hideScore();
    Hud.hideWatch();
    const mission = World.getMission() ?? loginCamera.mission;

    camera = new Cinematic(mission === loginCamera.mission ? loginCamera.splines : []);
    recorder = new CameraRecorder(
        camera,
        (value) => {
            if (view !== null && pageReady) {
                Web.emit(view, EVENT.recorderState, value);
            }
        },
        (paths) => Events.emitServer(EVENT.cameraSave, paths),
        mission,
        () => Fade.in(0.2),
    );

    nextHello = 0;
    Web.on(view, EVENT.ready, () => {
        pageReady = true;
        publish();
        recorder?.report();
    });

    Web.on(view, EVENT.recorder, (payload) => recorder?.command(payload));
    Web.on(view, EVENT.rememberedReady, () => Events.emitServer(EVENT.rememberedReady));
    Web.on(view, EVENT.forget, (payload) => Events.emitServer(EVENT.forget, payload));

    Web.on(view, EVENT.auth, (payload) => {
        if (state.pending || state.phase !== 'auth' || !state.connected || !isRecord(payload)) {
            return;
        }

        state = { ...state, pending: true, error: undefined };
        pendingSince = Date.now();
        Events.emitServer(EVENT.auth, { ...payload, generation: state.generation });
        publish();
    });

    Web.on(view, EVENT.motion, (payload) => {
        if (camera && isRecord(payload) && typeof payload.reduced === 'boolean') {
            camera.reducedMotion = payload.reduced;

            // A pause during a dissolve must not leave the background black.
            if (payload.reduced) {
                Fade.in(0.2);
            }
        }
    });
}

events.on(EVENT.state, (payload) => {
    if (
        view === null ||
        !isRecord(payload) ||
        payload.generation !== state.generation ||
        !['auth', 'spawning', 'playing'].includes(String(payload.phase))
    ) {
        return;
    }

    state = {
        ...state,
        ...payload,
        remembered: payload.remembered,
        forgetRemembered: payload.forgetRemembered === true,
        connected: true,
        pending: payload.phase === 'spawning',
    } as ViewState;

    publish();

    if (recorder) {
        recorder.enabled = state.cameraEditor === true;
    }

    // Wait for the local replica, which can arrive after the script event.
    if (payload.phase === 'playing' && LocalPlayer?.spawned) {
        close();
    }
});

events.on(EVENT.cameraPaths, (payload) => {
    if (isCameraPaths(payload) && payload.mission === World.getMission()) {
        camera?.setSplines(payload.splines);
        recorder?.load(payload);
    }
});

events.on(EVENT.cameraSaved, (payload) => {
    if (isRecord(payload)) {
        recorder?.saved(payload.ok === true);
    }
});

events.on('missionReady', open);
events.on('missionUnload', close);
events.on('resourceStart', (name) => {
    if (name === RESOURCE) {
        open();
    }
});

events.on('resourceStop', (name) => {
    if (name === RESOURCE) {
        close();
    }
});

events.on('playerSpawn', (player) => {
    if (player.isLocal) {
        close();
    }
});

events.on('render', () => {
    const now = Date.now();
    const delta = Math.max(0, Math.min(now - previousFrame, 100));

    previousFrame = now;

    if (view === null) {
        open();

        return;
    }

    if (LocalPlayer?.spawned) {
        close();

        return;
    }

    camera?.update(delta);
    recorder?.update(delta);

    if (!state.connected && now >= nextHello) {
        Events.emitServer(EVENT.ready, { generation: state.generation });
        nextHello = now + 1000;
    }

    if (state.phase === 'auth' && state.pending && now - pendingSince > 15_000) {
        state = { ...state, pending: false, error: 'timeout' };
        publish();
    }
});
