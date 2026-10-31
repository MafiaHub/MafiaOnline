// The home decorating keys (server/housing/decorating.ts). Only while the server says
// the player is decorating: Home/End (or numpad 4/6) turn the piece being
// carried, a tap by a step and held on smoothly, PgUp/PgDn (or numpad 8/2)
// raise and lower it, Delete sells it back and numpad 0 puts it back, or
// ends decorating when nothing is carried. The mouse wheel is left alone:
// the game switches weapons with it. G, which picks up and places, is the
// job key (client/rescue.ts).
import { events, onServer } from './lib/runtime';

type Turn = 1 | -1;
type DecorRequest =
    { turn: Turn } | { spin: Turn | 0 } | { lift: 1 | -1 } | { sell: true } | { cancel: true };

// A turn key held this long starts turning the piece on until it is let go.
const HOLD_MS = 300;

const TURN_KEYS: readonly (readonly [key: string, turn: Turn, label: string])[] = [
    ['end', 1, 'Decorating: turn right (hold to keep turning)'],
    ['home', -1, 'Decorating: turn left (hold to keep turning)'],
    ['numpad6', 1, 'Decorating: turn right (2nd)'],
    ['numpad4', -1, 'Decorating: turn left (2nd)'],
];

const DECOR_KEYS: readonly (readonly [key: string, request: DecorRequest, label: string])[] = [
    ['pageup', { lift: 1 }, 'Decorating: raise'],
    ['pagedown', { lift: -1 }, 'Decorating: lower'],
    ['numpad8', { lift: 1 }, 'Decorating: raise (2nd)'],
    ['numpad2', { lift: -1 }, 'Decorating: lower (2nd)'],
    ['delete', { sell: true }, 'Decorating: sell the piece back'],
    ['numpad0', { cancel: true }, 'Decorating: put back or finish'],
];

let decorating = false;
// The turn key held now, and whether it has started the piece turning.
let held: { key: string; turn: Turn; since: number; spinning: boolean } | null = null;

function send(request: DecorRequest): void {
    events.emitServer('fr:home:edit', request);
}

function release(): void {
    if (held?.spinning) {
        send({ spin: 0 });
    }

    held = null;
}

onServer<{ on?: boolean }>('fr:home:edit', (data) => {
    decorating = Boolean(data.on);

    if (!decorating) {
        held = null;
    }
});

for (const [key, turn, label] of TURN_KEYS) {
    Settings.describeKey(key, label);
    Key.bind(key, 'down', () => {
        if (!decorating) {
            return;
        }

        release();
        send({ turn });
        held = { key, turn, since: Date.now(), spinning: false };
    });

    Key.bind(key, 'up', () => {
        if (held?.key === key) {
            release();
        }
    });
}

for (const [key, request, label] of DECOR_KEYS) {
    Settings.describeKey(key, label);
    Key.bind(key, () => {
        if (decorating) {
            send(request);
        }
    });
}

// A release the bindings never saw (a page took the keyboard, the window lost
// focus) still stops the turning: Key.isDown is false then.
events.on('render', () => {
    if (!held) {
        return;
    }

    if (!decorating || !Key.isDown(held.key)) {
        release();

        return;
    }

    if (!held.spinning && Date.now() - held.since >= HOLD_MS) {
        held.spinning = true;
        send({ spin: held.turn });
    }
});

events.on('missionUnload', () => {
    decorating = false;
    held = null;
});
