// The rum distillery: a hidden still at an old woodcutters' camp in the
// lowland woods, on Lost Heaven Extended. Nothing marks it on the map until a
// player learns of it. A short quest chain leads there (quest.ts). After
// that the loop repeats: fetch molasses (G in a car stopped at the village
// store), fire the still (G) and keep its pressure gauge in the green by
// tapping G to vent (it blows up past the top), let the cask age, bottle it
// (hold G), load a car in the clearing and drive the cases to Luigi at
// Salieri's (safe, pays least), the speakeasy at Morello's (the police may
// notice) or Paco at the docks (pays most, the city is told and anyone may
// drive the car in). Every batch and sale raises the operation's heat, which
// cools by itself, by a bribe at Salieri's (/rum bribe) or by paying
// Sergeant Doyle when he turns up at the camp; at full heat the revenue men
// come for the still (after a warning: defend it or bribe them off).
// Morello's boys come for a big stock of cases, another player may steal
// cases while an owner is online, and a wedding in the village sometimes
// orders rum. A crew shares one operation.
//
// places.ts       where everything is, and why there
// operations.ts   quest progress, the operations and their stock, saving
// characters.ts   Hank and the camp's voices
// copper.ts       the hardware truck and its copper worm
// quest.ts        Ernesto, Hank's talk and finding the camp
// still.ts        the still and its gauge, the blast, the props
// casks.ts        ageing and bottling
// cargo.ts        molasses and rum on the road, the buyers, theft
// heat.ts         heat and bribes
// raids.ts        the revenue men, Morello's boys, Sergeant Doyle
// orders.ts       the village wedding, party night at Salieri's
// actions.ts      what the job key does where
// presentation.ts the card, compass and markers
// commands.ts     /rum and the city menu
import { events, every, onStart, onStop } from '../lib/lifecycle';
import { toast } from '../lib/notify';
import { interactions } from './actions';
import {
    returnLoads,
    returnStolen,
    dropStolen,
    dropYardStop,
    forgetRumours,
    showRumours,
    tickCargo,
    tickYard,
} from './cargo';
import { age } from './casks';
import { hank, tickHank } from './characters';
import './commands';
import { dropAllCoils, dropCoil, forgetTruck, removeTruck, tickTruck } from './copper';
import { cool } from './heat';
import { available, changed, cooldownOf, load, ops, persist, saveIfDue, TITLE } from './operations';
import { clearWeddings, expireWeddings, rollParty, rollWedding, scheduleParty } from './orders';
import { CAMP_RADIUS } from './places';
import { clearHud, forgetShown, present } from './presentation';
import { tickQuest } from './quest';
import {
    defendersOf,
    dismissVisitors,
    forgetVisitors,
    rollVisit,
    tickDoyle,
    tickRaid,
    tickRevenue,
} from './raids';
import {
    bank,
    bankFire,
    batchOf,
    clearBlast,
    clearProps,
    forgetProps,
    returnBatch,
    syncProps,
    tickStill,
} from './still';

export { knows, stageOf } from './operations';

const TICK_MS = 250;
const PRESENT_MS = 1000;
const MAX_STEP_MS = 2000;
const EVENT_ROLL_MS = 60_000;

let lastTick = 0;
let lastPresentAt = 0;
let lastEventRollAt = 0;

function rollEvents(now: number): void {
    for (const [opKey, op] of ops) {
        if (!op.unlocked) {
            continue;
        }

        const wait = cooldownOf(opKey);

        rollVisit(opKey, op, defendersOf(opKey, CAMP_RADIUS), wait, now);
        rollWedding(opKey, op, wait, now);
    }

    expireWeddings(now);
    rollParty(now);
}

function tick(): void {
    const now = Date.now();
    const dt = Math.min(MAX_STEP_MS, Math.max(0, now - lastTick));

    lastTick = now;

    if (!available() || !World.isReady()) {
        return;
    }

    syncProps();
    tickStill(dt, now);
    age(dt);
    cool(dt);
    interactions.tickHolds(now);
    tickYard(now);
    tickCargo();
    interactions.showHints();

    if (now - lastPresentAt >= PRESENT_MS) {
        lastPresentAt = now;
        tickQuest();
        tickHank(now);
        tickTruck(now);
        tickRaid(now);
        tickDoyle(now);
        tickRevenue(now);

        for (const player of World.getPlayers()) {
            present(player);
        }
    }

    if (now - lastEventRollAt >= EVENT_ROLL_MS) {
        lastEventRollAt = now;
        rollEvents(now);
    }

    saveIfDue(now);
}

// A batch on the still, a load in a car and cases in a thief's hands go back
// to their operation; a carried worm is gone.
function returnAll(): void {
    returnLoads();
    dropAllCoils();
    returnBatch();
    changed();
}

load();

onStart(() => {
    lastTick = Date.now();
    scheduleParty(Date.now());
    every(TICK_MS, tick);
});

onStop(() => {
    for (const player of World.getPlayers()) {
        interactions.prompt(player, null);
        clearHud(player, true);
    }

    dismissVisitors();
    hank.remove();
    removeTruck();
    returnAll();
    clearBlast();
    persist();
    clearProps();
});

events.on('playerDeath', (player) => {
    if (dropCoil(player)) {
        toast(
            player,
            'You dropped the copper worm. The dealer will have another on the truck soon.',
            {
                title: TITLE,
                tone: 'bad',
            },
        );
    }

    if (dropStolen(player)) {
        toast(player, 'The stolen cases smashed when you went down.', {
            title: 'Rum',
            tone: 'bad',
        });
    }

    interactions.drop(player);
    dropYardStop(player);
    const batch = batchOf(player);

    if (batch) {
        bankFire(batch);
    }
});

events.on('playerSpawn', (player) => {
    forgetShown(player);
    interactions.forget(player);
    showRumours(player);
});

events.on('playerDisconnect', (player) => {
    returnStolen(player);
    const batch = batchOf(player);

    if (batch) {
        bank(batch);
    }
});

events.on('missionChange', () => {
    for (const player of World.getPlayers()) {
        interactions.prompt(player, null);
        clearHud(player, false);
    }

    returnAll();
    clearBlast();
    // The mission's objects, vehicles and NPCs went with it; the revenue men
    // set out afresh, with a fresh warning, once the map is back.
    forgetVisitors();
    hank.forget();
    forgetTruck();
    forgetProps();
    clearWeddings();
    forgetRumours();
    persist();
});
