// Shows what server/police.ts decides: the retail police lines, the wanted
// badge and the arrest fade. Fines and bail come out of the cash balance,
// which the score counter mirrors. On police duty it also offers the call
// box under the player's feet (server/police_duty/backup.ts) and plays the
// telephone and radio sounds of a call; the server checks every call.
import { flatDistance, type Point } from './lib/geometry';
import { drawPrompt, keyHint } from './lib/panels';
import { livePlayer } from './lib/player';
import { events, onServer } from './lib/runtime';

const CALL_KEY = 'u';

type PoliceMessage =
    | { action: 'text'; textId: number }
    | { action: 'wanted'; level?: number }
    | { action: 'fine' }
    | { action: 'arrest' }
    | { action: 'booths'; reach?: number; booths?: Point[] }
    | { action: 'sound'; wave?: string; volume?: number };

// The call boxes a duty officer can use, and how close they must stand.
let reach = 0;
let booths: Point[] = [];

function atBooth(): boolean {
    const player = livePlayer();

    if (!booths.length || !player || player.getVehicle()) {
        return false;
    }

    const position = player.position;

    return booths.some(
        (booth) => flatDistance(booth, position) <= reach && Math.abs(booth.y - position.y) <= 2.5,
    );
}

onServer<PoliceMessage>('fr:police', (data) => {
    switch (data.action) {
        case 'text':
            Hud.showGameText(data.textId);
            break;
        case 'wanted':
            Hud.setWanted(data.level ?? 0);
            break;
        case 'fine':
            Sound.play('01b_srdce.wav', { volume: 0.5 });
            break;
        case 'arrest':
            Hud.setWanted(0);
            Fade.out(1);
            setTimeout(() => Fade.in(1), 3500);
            break;
        case 'booths':
            reach = Number(data.reach) || 0;
            booths = Array.isArray(data.booths) ? data.booths : [];
            break;
        case 'sound':
            if (typeof data.wave === 'string') {
                Sound.play(data.wave, { volume: Number(data.volume) || 0.8 });
            }

            break;
    }
});

Settings.describeKey(CALL_KEY, 'Police: call for backup at a phone booth');
Key.bind(CALL_KEY, () => {
    if (atBooth()) {
        events.emitServer('fr:backup:call', {});
    }
});

events.on('render', () => {
    if (atBooth()) {
        drawPrompt(`${keyHint(CALL_KEY)} CALL THE PRECINCT FOR BACKUP`, { rise: 96, width: 380 });
    }
});

events.on('missionUnload', () => {
    booths = [];
});
