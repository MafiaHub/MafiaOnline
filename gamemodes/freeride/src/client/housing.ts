// The home decorating keys (server/housing/decorating.ts). Only while the server says
// the player is decorating: Home/End (or numpad 4/6) turn the piece being
// carried, PgUp/PgDn (or numpad 8/2) raise and lower it, Delete sells it back
// and numpad 0 puts it back, or ends decorating when nothing is carried. The
// mouse wheel is left alone: the game switches weapons with it. G, which picks
// up and places, is the job key (client/rescue.ts).
import { events, onServer } from './lib/runtime';

type DecorRequest = { turn: 1 | -1 } | { lift: 1 | -1 } | { sell: true } | { cancel: true };

const DECOR_KEYS: readonly (readonly [key: string, request: DecorRequest, label: string])[] = [
    ['end', { turn: 1 }, 'Decorating: turn right'],
    ['home', { turn: -1 }, 'Decorating: turn left'],
    ['numpad6', { turn: 1 }, 'Decorating: turn right (2nd)'],
    ['numpad4', { turn: -1 }, 'Decorating: turn left (2nd)'],
    ['pageup', { lift: 1 }, 'Decorating: raise'],
    ['pagedown', { lift: -1 }, 'Decorating: lower'],
    ['numpad8', { lift: 1 }, 'Decorating: raise (2nd)'],
    ['numpad2', { lift: -1 }, 'Decorating: lower (2nd)'],
    ['delete', { sell: true }, 'Decorating: sell the piece back'],
    ['numpad0', { cancel: true }, 'Decorating: put back or finish'],
];

let decorating = false;

onServer<{ on?: boolean }>('fr:home:edit', (data) => {
    decorating = Boolean(data.on);
});

for (const [key, request, label] of DECOR_KEYS) {
    Settings.describeKey(key, label);
    Key.bind(key, () => {
        if (decorating) {
            events.emitServer('fr:home:edit', request);
        }
    });
}

events.on('missionUnload', () => {
    decorating = false;
});
