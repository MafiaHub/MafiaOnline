// The medicine cabinets on the walls of Lost Heaven Extended. Each is a door
// (the cabinet's lid) that the mod's mission script opened on use, healing the
// player, and then shut and kept shut for four or five minutes. Mission scripts
// do not run online, so native use only swung the lid: the server plays the
// script's part here. Names, amounts and waits come from the mod's
// freeride_extended scene2.bin, one script per cabinet:
//
//   findactor 1, "9lekarna.Box02"   door_enableus 1,0
//   detector_waitforuse ...         door_open 1,1
//   human_getproperty 0,0,Energy    let flt[0]=flt[0]+30   (capped at 100)
//   human_setproperty 0,flt[0],Energy   wait 500   door_open 1,0
//   wait 300000                     goto 1
//
// The wait is kept per player rather than per cabinet, so one player emptying
// a cabinet does not keep it from everyone else.
import { toast } from './lib/notify';
import { events, onMissionChange } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { formatMinutes, playable } from './lib/util';

interface Cabinet {
    heal: number;
    restockMs: number;
}

const CABINETS: Readonly<Record<string, Cabinet>> = {
    'blekarna.Box02': { heal: 50, restockMs: 240_000 },
    '9lekarna.Box02': { heal: 30, restockMs: 300_000 },
    'lekarna_hom.Box02': { heal: 30, restockMs: 240_000 },
    'lekarn_aut.Box02': { heal: 30, restockMs: 240_000 },
    'air_9lekarna.Box02': { heal: 40, restockMs: 240_000 },
    'air_9lekarna2.Box02': { heal: 40, restockMs: 240_000 },
    'pr_9lekarna.Box02': { heal: 50, restockMs: 240_000 },
    'plh_9lekarna4.Box02': { heal: 30, restockMs: 240_000 },
    'plh_9lekarna.Box02': { heal: 50, restockMs: 240_000 },
};

const TITLE = 'First aid';
const MAX_HEALTH = 100;
// The script shut the lid half a second after the bandaging animation; with
// no animation online, the lid stays open long enough to be seen.
const CLOSE_AFTER_MS = 1500;

// Player -> cabinet frame -> when they may use it again.
const restocked = new PlayerMap<Map<string, number>>();
const closing = new Map<string, ReturnType<typeof setTimeout>>();

function shutLater(door: Door): void {
    const name = door.frameName;

    clearTimeout(closing.get(name));
    closing.set(
        name,
        setTimeout(() => {
            closing.delete(name);

            if (door.open) {
                door.closeDoor();
            }
        }, CLOSE_AFTER_MS),
    );
}

function use(player: Player, door: Door, cabinet: Cabinet): void {
    const now = Date.now();
    const waits = restocked.get(player.id) ?? new Map<string, number>();
    const readyAt = waits.get(door.frameName) ?? 0;

    if (player.health >= MAX_HEALTH) {
        toast(player, 'You are not hurt.', { title: TITLE, seconds: 3 });

        return;
    }

    if (now < readyAt) {
        toast(player, `The cabinet is empty. Restocked in ${formatMinutes(readyAt - now)}.`, {
            title: TITLE,
            tone: 'bad',
            seconds: 4,
        });

        return;
    }

    const before = player.health;

    player.setHealth(Math.min(MAX_HEALTH, before + cabinet.heal));
    waits.set(door.frameName, now + cabinet.restockMs);
    restocked.set(player.id, waits);
    toast(player, `You patch yourself up: +${Math.round(player.health - before)} health.`, {
        title: TITLE,
        tone: 'good',
        seconds: 3,
    });
}

events.on('doorStateChange', (door, player) => {
    const cabinet = CABINETS[door.frameName];

    // Our own closeDoor fires this too, with no player.
    if (!cabinet || !player || !door.open) {
        return;
    }

    shutLater(door);

    if (playable(player)) {
        use(player, door, cabinet);
    }
});

onMissionChange(() => {
    for (const timer of closing.values()) {
        clearTimeout(timer);
    }

    closing.clear();
});
