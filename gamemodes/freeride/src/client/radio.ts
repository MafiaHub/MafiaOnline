// The car radio is a client-only HTML audio view. The directory page is not
// an audio source; radio.html plays the station's direct live MP3 stream.
import { COLORS } from './lib/color';
import { Page } from './lib/page';
import { livePlayer, onLocalDeath } from './lib/player';
import { RESOURCE, events, onServer, onStop } from './lib/runtime';

const STREAM_TIMEOUT_MS = 12_000;

// The station plays this loud at full media volume (Esc → Settings → Audio).
const STREAM_VOLUME = 0.72;
const RADIO_KEY = 'f9';
const TOGGLE = 'radio';

// The view is made once and kept: a browser started on every car entry was
// a hitch exactly as the player got in. Only play and stop go to it.
const page = new Page(
    'Web radio view',
    'radio.html',
    { width: 1, height: 1, x: 0, y: 0, zIndex: -100, visible: true, focus: false },
    // A page still loading plays once it is ready.
    () => {
        if (vehicleId !== null) {
            page.emit('radio:play', { volume: streamVolume() });
        }
    },
);

// The car it plays in, and whether the stream has started there.
let vehicleId: number | null = null;
let online = false;
let startedAt = 0;
let reportedFailure = false;
let enabled = true;

// The player may move F9 and flip the radio on the settings screen.
Settings.describeKey(RADIO_KEY, 'Car radio on or off');
Settings.defineToggle(TOGGLE, 'Car radio', enabled);

function streamVolume(): number {
    return STREAM_VOLUME * Settings.getMediaVolume();
}

function say(message: string): void {
    Hud.showMessage(`RADIO · ${message}`, COLORS.message);
}

function reportFailure(): void {
    if (vehicleId === null || reportedFailure) {
        return;
    }

    online = false;
    reportedFailure = true;
    say('Station unavailable');
}

function stopRadio(): void {
    if (vehicleId !== null) {
        page.emit('radio:stop');
    }

    vehicleId = null;
    online = false;
    startedAt = 0;
    reportedFailure = false;
}

function destroyRadio(): void {
    stopRadio();
    page.close();
}

function openPage(): boolean {
    if (page.isOpen) {
        return true;
    }

    if (!page.open()) {
        return false;
    }

    page.on('radio:playing', () => {
        if (vehicleId === null) {
            return;
        }

        online = true;
        reportedFailure = false;
        say('Jazz Club Bandstand');
    });

    page.on('radio:failed', reportFailure);

    return true;
}

function startRadio(vehicle: Vehicle): void {
    stopRadio();
    vehicleId = vehicle.id;
    startedAt = Date.now();

    if (!openPage()) {
        reportFailure();

        return;
    }

    page.emit('radio:play', { volume: streamVolume() });
}

function setRadio(on: boolean, announce: boolean): void {
    enabled = on;
    Settings.setToggle(TOGGLE, on);

    if (!on) {
        stopRadio();

        if (announce) {
            say(`Off (${Settings.getKeyLabel(RADIO_KEY)} or /radio to tune in)`);
        }

        return;
    }

    const vehicle = livePlayer()?.getVehicle() ?? null;

    if (vehicle && vehicleId === null) {
        startRadio(vehicle);
    }

    if (announce) {
        say(vehicle ? 'Tuning in…' : 'On for your next car');
    }
}

// The server keeps the choice with the player's record (/radio does the same).
Key.bind(RADIO_KEY, () => {
    setRadio(!enabled, true);
    events.emitServer('fr:radio:set', { enabled });
});

onServer<{ enabled?: boolean; announce?: boolean }>('fr:radio:setting', (data) =>
    setRadio(data.enabled !== false, data.announce === true),
);

events.on('settingToggle', (id, value, resource) => {
    if (resource !== RESOURCE || id !== TOGGLE || value === enabled) {
        return;
    }

    setRadio(value, false);
    events.emitServer('fr:radio:set', { enabled: value });
});

events.on('mediaVolumeChange', () => {
    page.emit('radio:volume', { volume: streamVolume() });
});

events.on('missionUnload', stopRadio);
onLocalDeath(stopRadio);
onStop(destroyRadio);

events.on('render', () => {
    const vehicle = livePlayer()?.getVehicle() ?? null;

    if (!enabled) {
        return;
    }

    if ((vehicle?.id ?? null) !== vehicleId) {
        if (vehicle) {
            startRadio(vehicle);
        } else if (vehicleId !== null) {
            stopRadio();
        }
    }

    if (vehicleId !== null && !online && Date.now() - startedAt > STREAM_TIMEOUT_MS) {
        reportFailure();
    }
});
