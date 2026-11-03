// Runs in each client's sandboxed V8. The server owns the world; this script
// drives the fades, announcements and sounds that frame each moment.
import { clamp } from './lib/geometry';
import { onLocalDeath, onLocalSpawn } from './lib/player';
import { events, onServer } from './lib/runtime';
import { Timers } from './lib/timers';

const COUNTDOWN_BEEP = 'beep50.wav';
const CHECKPOINT_SOUND = '00_kompas.wav';

const countdown = new Timers();

events.on('missionReady', () => Fade.in(1.5));

onLocalSpawn(() => Fade.in(1));

onLocalDeath(() => {
    countdown.clear();
    Sound.play('01b_srdce.wav');
    Fade.out(1.2);
});

events.on('missionUnload', () => countdown.clear());

// "Get ready", then the given number of seconds, then the green light; the
// server releases the cars at the same moment.
onServer<{ seconds?: number }>('fr:race:countdown', ({ seconds }) => {
    countdown.clear();

    const count = clamp(seconds ?? 3, 1, 5);

    Hud.announce('GET READY', 1);

    for (let step = 1; step <= count; step++) {
        countdown.after(step * 1000, () => {
            Hud.announce(String(count - step + 1), 1);
            Sound.play(COUNTDOWN_BEEP, { volume: 0.7 });
        });
    }

    countdown.after((count + 1) * 1000, () => {
        Hud.announce('GO!', 1.5);
        Sound.play(COUNTDOWN_BEEP, { volume: 1 });
        Sound.play('18bc_start.wav', { volume: 0.8 });
    });
});

events.on('fr:race:checkpoint', () => Sound.play(CHECKPOINT_SOUND, { volume: 0.8 }));

// Walking through a door: the server moves the player while the screen is dark.
onServer<{ out?: boolean; seconds?: number }>('fr:fade', ({ out, seconds }) => {
    if (out) {
        Fade.out(seconds ?? 0.4);
    } else {
        Fade.in(seconds ?? 0.6);
    }
});

onServer<{ text?: string; seconds?: number; sound?: string | null }>(
    'fr:announce',
    ({ text, seconds, sound }) => {
        if (typeof text === 'string') {
            Hud.announce(text, seconds ?? 2);
        }

        if (typeof sound === 'string') {
            Sound.play(sound, { volume: 0.8 });
        }
    },
);

// /dev hud on a server started with FREERIDE_DEV=1.
onServer<{ action?: string; value?: string }>('fr:dev:hud', ({ action, value }) => {
    const number = Number(value);

    switch (action) {
        case 'countdown':
            Hud.startCountdown(Number.isFinite(number) ? number : 30);
            break;
        case 'watch':
            Hud.showWatch(12, 0);
            break;
        case 'swing':
            Camera.setSwing(Number.isFinite(number) ? number : 30);
            break;
        case 'fade':
            Fade.out(1);
            setTimeout(() => Fade.in(1), 2000);
            break;
        case 'clear':
            Hud.hideWatch();
            Camera.setSwing(0);
            break;
        default:
            if (Sound.play(typeof value === 'string' ? value : '00_dog.wav') === null) {
                Hud.showMessage('Could not play that sound');
            }
    }
});
