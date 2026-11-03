// The story that leads to the still:
//   1. Ernesto, the sailor who drinks in Salieri's billiard room, tells of a
//      Cuban moonshiner hiding in the woods west of the lowland road.
//   2. The player finds the camp; old Hank runs it and needs copper.
//   3. The player unbolts the copper worm off a hardware dealer's truck in
//      the village (the police are called) and brings it to Hank.
//   4. First fire: molasses from the village store, a batch on the still,
//      the cask ages, the rum is bottled.
//   5. The first load sold. Then Paco, Ernesto's cousin at the docks, buys.
import * as profiles from '../lib/profiles';
import { stoppedAt } from '../lib/errands';
import { npcsAvailable, speak } from '../lib/npcs';
import { toast } from '../lib/notify';
import { emit, flatDistance, formatMoney, onStreet, pick, plural, speedOf } from '../lib/util';
import { MOLASSES_PRICE, SACKS_PER_RUN } from './cargo';
import { agedCask } from './casks';
import { hank, hankSays, LINE } from './characters';
import { carrying, deliverCoil } from './copper';
import { heatWord } from './heat';
import { changed, opKeyOf, opOf, QUEST, recordOf, stageOf, TITLE, XP } from './operations';
import {
    DISCOVER_RADIUS,
    driving,
    POT,
    REVEAL_CAMERA,
    SAILOR,
    STOPPED_SPEED,
    YARD,
    YARD_RADIUS,
} from './places';
import { unpoint } from './presentation';
import { stillBatch } from './still';

const ERNESTO_MODEL = 'sailor03.i3d';
const ERNESTO_LINES = [
    "The sea's no place for an old man. Neither is Lost Heaven.",
    'Hank still alive? Tell him he owes me a bottle.',
    "My cousin Paco works the container docks. Ask him about rum when you've got some.",
];

export function talkToSailor(player: Player): void {
    const record = recordOf(player);
    const sailor = npcsAvailable()
        ? (Npc.getInRange(SAILOR, 2).find((npc) => npc.model.toLowerCase() === ERNESTO_MODEL) ??
          null)
        : null;

    sailor?.face(player.position);
    unpoint(player);

    if (record && record.stage === QUEST.ASK && !opOf(player)?.unlocked) {
        record.stage = QUEST.FIND;
        changed();
        profiles.addXp(player, XP.lead, "A sailor's tale");
        speak(
            player,
            'Ernesto',
            "Rum? Real Cuban rum, not this bathtub poison? There's an old man, Hank, cooks it in the woods west of the lowland road, " +
                'past the fenced pasture where the woodcutters left their logs. Tell him Ernesto sent you.',
            { npc: sailor, voice: LINE.chat, seconds: 12 },
        );

        toast(player, 'Your compass points the way Ernesto described. Hold TAB for the map.', {
            title: TITLE,
            tone: 'gold',
            seconds: 6,
        });

        return;
    }

    if (record?.stage === QUEST.FIND) {
        speak(
            player,
            'Ernesto',
            'West of the lowland road, past the fenced pasture, where the woodcutters left their logs. Follow your nose.',
            { npc: sailor, voice: LINE.chat, seconds: 8 },
        );

        return;
    }

    speak(player, 'Ernesto', pick(ERNESTO_LINES), { npc: sailor, voice: LINE.chat, seconds: 6 });
}

export function talkToHank(player: Player): void {
    const op = opOf(player);
    const stage = stageOf(player);
    const batch = stillBatch();

    if (!op) {
        return;
    }

    if (stage === QUEST.COPPER) {
        hankSays(
            player,
            carrying(player)
                ? "That's the worm! Bring it here to the still."
                : "The revenue men smashed my copper worm. A hardware dealer's truck stands in the village north of the store: unbolt the worm off its bed.",
        );
    } else if (batch && batch.operatorId === null && batch.opKey === opKeyOf(player)) {
        hankSays(
            player,
            "The fire's banked. Press G at the still and keep the gauge in the green.",
        );
    } else if (agedCask(op) >= 0) {
        hankSays(player, "A cask's ready. Bottle it at the casks.");
    } else if (op.cases.n) {
        hankSays(
            player,
            `${plural(op.cases.n, 'case')} in the shed. Drive a car into the clearing, stop by the shed and we'll load it.`,
        );
    } else if (!op.molasses) {
        hankSays(
            player,
            `No molasses. The village store sells it: stop a car out front and press G. ${formatMoney(MOLASSES_PRICE)} for ${SACKS_PER_RUN} sacks.`,
        );
    } else {
        hankSays(
            player,
            `${plural(op.molasses, 'sack')} of molasses. Fire up the still whenever you're ready. Heat's ${heatWord(op.heat)}.`,
        );
    }

    hank.present()?.face(player.position);
}

/* Discovery */

export function tickQuest(): void {
    for (const player of World.getPlayers()) {
        if (!onStreet(player)) {
            continue;
        }

        const record = recordOf(player);
        // On foot or stopped: a locked shot of the still is no way to end a drive.
        const car = player.getVehicle();
        const settled = !car || speedOf(car) < 2;

        if (
            settled &&
            record?.stage === QUEST.FIND &&
            !opOf(player)?.unlocked &&
            flatDistance(player.position, POT) <= DISCOVER_RADIUS
        ) {
            record.stage = QUEST.COPPER;
            changed();
            profiles.addXp(player, XP.find, 'Found the still');
            emit(player, 'fr:rum:reveal', { ...REVEAL_CAMERA, title: 'THE STILL IN THE WOODS' });
            setTimeout(() => {
                if (!onStreet(player)) {
                    return;
                }

                hankSays(
                    player,
                    'Ernesto sent you? Then you can help. The revenue men smashed my copper worm, and without one this pot makes nothing. ' +
                        "A hardware dealer's truck stands in the village, north of the store. Bring me the worm off its bed.",
                    LINE.nod,
                    12,
                );
            }, 4500);
        }

        // A coil carried into the clearing in a car counts too.
        const vehicle = driving(player);

        if (
            vehicle &&
            carrying(player) &&
            stageOf(player) === QUEST.COPPER &&
            stoppedAt(vehicle, YARD, YARD_RADIUS, STOPPED_SPEED)
        ) {
            deliverCoil(player);
        }
    }
}
