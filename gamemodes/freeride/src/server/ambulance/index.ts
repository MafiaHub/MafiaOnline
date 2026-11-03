// The ambulance. A player signs on at Lost Heaven Hospital (places.ts; the
// sign-on arrow is at the foot of the steps) and takes one of the stock
// ambulances parked on the forecourt (lib/stations.ts keeps them there;
// duty.ts). Calls come in from the street: someone collapsed, someone badly
// hurt, a smoke victim the fire brigade carried out, and players who went
// down while a medic is on duty: they wait for one before the usual respawn
// (lib/life.ts holds it; scenes.ts). The medic drives there, holds G beside
// the patient to treat them (care.ts) and either leaves them on their feet
// or takes them to the hospital, riding in the ambulance, before the time
// runs out for a tip (ride.ts).
//
// Some calls are more than that. A woman in labour rides in for a bigger fare
// and a tip for a smooth ride: every bump makes her cry out. A heart attack
// needs CPR on the pavement, G tapped in a steady rhythm, before his time
// runs out; then he rides in. A pile-up leaves three people hurt around two
// wrecks, tagged critical, serious and minor: the critical one bleeds out
// unless treated first, then rides in, the other two are patched up where
// they sit, and with the fire brigade on duty one wreck burns beside them
// (firefighters/). The patients talk in their own voices: moaning while
// they wait, crying out at a crash.
import * as commands from '../lib/commands';
import * as jobkey from '../lib/jobkey';
import * as menu from '../lib/menu';
import { events, onStop } from '../lib/lifecycle';
import { calls as board, endAll, forgetAll, type Call } from './board';
import { medicKey, medics } from './duty';
import { hospital } from './places';
import { forgetArrivals } from './ride';
import { clock, streetCall } from './scenes';

jobkey.register(medicKey);

// Signing on happens under the hospital's arrow; the command shows the way
// there and clocks off from anywhere.
commands.register(
    {
        name: 'medic',
        aliases: ['ambulance', 'paramedic'],
        usage: '/medic',
        help: "Show the way to the hospital's sign-on point, or clock off.",
        category: 'City',
    },
    (player) => {
        if (medics.has(player)) {
            medics.stop(player, 'Off duty.');
        } else {
            medics.start(player);
        }
    },
);

menu.provide('medic', (player) => ({
    onDuty: medics.has(player),
    medics: medics.size,
    available: Boolean(hospital()),
    calls: [...board.values()].filter((call) => call.state === 'waiting').length,
    earned: medics.get(player)?.earned ?? 0,
}));

// After the shift's own (duty.ts): the medics are off duty before the calls go.
onStop(() => {
    endAll();
    forgetArrivals();
});

events.on('missionChange', () => {
    forgetAll();
    forgetArrivals();
});

export function onDuty(player: Player): boolean {
    return medics.has(player);
}

/** Every call open or on the way in. */
export function calls(): Call[] {
    return [...board.values()];
}

/** The next street call comes in `ms` from now. */
export function schedule(ms: number): void {
    clock.after(ms);
}

export { streetCall };
export type { Call };
export type { StreetCallKind } from './scenes';
