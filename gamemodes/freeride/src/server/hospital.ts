// Lost Heaven Hospital, on maps that have one (lib/maps.ts). The dead wake up
// at its doors instead of their district, and the hospital sends a bill.
// The Dispensary's attendant stands at the same doors and sells bandages.
import * as maps from './lib/maps';
import * as life from './lib/life';
import { COLORS, markers, toast } from './lib/notify';
import { events } from './lib/lifecycle';
import { formatMoney } from './lib/util';

const BILL = 50;

function hospital(): maps.Hospital | null {
    return maps.current()?.hospital ?? null;
}

// lib/life.ts asks this before the player's district.
life.setHospital(() => hospital()?.spawn ?? null);

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
