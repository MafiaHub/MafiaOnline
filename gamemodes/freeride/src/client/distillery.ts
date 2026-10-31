// The rum distillery's two moments (server/distillery/): the first look
// at the still when a player finds the camp, a short locked shot between
// fades, and the white flash of a still blowing up nearby.
import { clamp, toward, type Point } from './lib/geometry';
import { onLocalDeath } from './lib/player';
import { events, onServer, onStop } from './lib/runtime';
import { Timers } from './lib/timers';

const REVEAL_HOLD_MS = 4200;
const WHITE = 0xffffff;

interface Reveal {
    /** Where the camera stands and what it looks at. */
    position?: Point;
    target?: Point;
    title?: string;
}

let revealing = false;
const shot = new Timers();

// Back to the player's camera, fading the screen in over this many seconds
// (0 at once: the shot was cut short, maybe while the screen was black).
function endReveal(seconds = 0.6): void {
    if (!revealing) {
        return;
    }

    shot.clear();
    revealing = false;
    Camera.unlock();
    Fade.in(seconds);
}

onServer<Reveal>('fr:rum:reveal', (data) => {
    const { position, target, title } = data;

    if (!position || !target) {
        return;
    }

    endReveal(0);
    revealing = true;
    Fade.out(0.5);
    shot.after(600, () => {
        if (!revealing) {
            return;
        }

        if (!Camera.lock(position, toward(position, target))) {
            endReveal();

            return;
        }

        Fade.in(0.8);

        if (typeof title === 'string') {
            Hud.announce(title, 3);
        }
    });

    shot.after(600 + REVEAL_HOLD_MS, () => {
        if (!revealing) {
            return;
        }

        Fade.out(0.4);
        shot.after(450, () => endReveal());
    });
});

onServer<{ strength?: number }>('fr:rum:flash', (data) => {
    const strength = typeof data.strength === 'number' ? clamp(data.strength, 0, 1) : 1;

    Fade.out(0.05, WHITE);
    setTimeout(() => Fade.in(0.4 + strength, WHITE), 80);
});

onLocalDeath(() => endReveal(0));
events.on('missionUnload', () => endReveal(0));
onStop(() => endReveal(0));
