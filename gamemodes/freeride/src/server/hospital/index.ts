// Lost Heaven Hospital, on maps that have one (lib/maps.ts). The dead wake up
// at its doors instead of their district, and the hospital sends a bill.
// Where it has wards, an arrow on the landing (lib/maps.ts,
// server/interiors.ts) leads into them: rooms built from map objects under
// the fields (lib/hospital_room.ts), staffed while anyone is inside
// (staff.ts). Their receptionist keeps the Dispensary's counter
// (server/shops.ts); on maps without wards its attendant stands on the
// landing.
import * as maps from '../lib/maps';
import * as life from '../lib/life';
import * as room from '../lib/hospital_room';
import { COLORS, markers, toast } from '../lib/notify';
import { events, every, onStart, onStop } from '../lib/lifecycle';
import { formatMoney } from '../lib/util';
import { forget as forgetStaff, forgetHint, showHints, standDown, tickStaff } from './staff';

const BILL = 50;
const TICK_MS = 1000;
const HINT_MS = 250;

function hospital(): maps.Hospital | null {
    return maps.current()?.hospital ?? null;
}

// lib/life.ts asks this before the player's district.
life.setHospital(() => hospital()?.spawn ?? null);

// The wards stand on the maps whose hospital has them.
function buildWards(): void {
    if (hospital()?.wards) {
        room.build();
    }
}

function tick(): void {
    if (hospital()?.wards) {
        tickStaff(Date.now());
    }
}

onStart(() => {
    buildWards();
    every(TICK_MS, tick);
    // The job key's prompt by the people inside, as fresh as other modules' hints.
    every(HINT_MS, showHints);
});

events.on('playerSpawn', forgetHint);

onStop(() => {
    standDown();
    room.teardown();
});

// The mission change removed every NPC and map object.
events.on('missionChange', () => {
    forgetStaff();
    room.forget();
    buildWards();
});

events.on('playerSpawn', (player) => {
    const place = hospital();

    markers(
        player,
        'hospital',
        place
            ? [
                  {
                      id: 'hospital',
                      position: place.door,
                      label: place.name.toUpperCase(),
                      color: COLORS.green,
                      range: 150,
                      map: false,
                  },
              ]
            : [],
    );
});

events.on('playerRespawn', (player) => {
    const place = hospital();

    if (!place || life.isTransit(player) || life.activity(player)) {
        return;
    }

    const bill = Math.min(player.money, BILL);

    if (bill > 0) {
        player.giveMoney(-bill);
    }

    toast(
        player,
        `The doctors patched you up${bill ? ` and billed you ${formatMoney(bill)}` : ''}.`,
        { title: place.name },
    );
});
