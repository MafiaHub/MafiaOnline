// The prisoner's view of server/jail/: the game's own countdown watch while the
// sentence runs, and a notice when the cell door opens. The server fades the
// screen around the moves in and out of the jail (fr:fade), and the jail's
// walls, doors and warden come from the server as map objects and an NPC.
import { events, onServer, onStop } from './lib/runtime';

interface JailState {
    active: boolean;
    /** What is left of the sentence, while it runs. */
    seconds?: number;
}

let counting = false;

onServer<JailState>('fr:jail', (state) => {
    if (state.active) {
        if (state.seconds !== undefined && Number.isInteger(state.seconds) && state.seconds > 0) {
            Hud.startCountdown(state.seconds);
            counting = true;
        }

        return;
    }

    counting = false;
    Hud.hideWatch();
    Hud.announce('CELL OPEN', 2);
});

// A run-out watch would stay up over the money counter while the warden
// comes to the door.
events.on('countdownEnd', () => {
    if (!counting) {
        return;
    }

    counting = false;
    Hud.hideWatch();
});

// The server moves prisoners out when Free Ride stops; the watch goes too.
onStop(() => Hud.hideWatch());
