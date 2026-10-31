// The retail police HUD, driven from the server: the simulation host runs the
// police for everyone and reports each player's crimes and wanted level; this
// resource decides what the player sees and what an arrest costs.
import * as life from './lib/life';
import * as jail from './jail';
import * as commands from './lib/commands';
import { toast, feed, name, text, COLORS } from './lib/notify';
import { emit, formatMoney } from './lib/util';
import { events } from './lib/lifecycle';
import { PlayerMap } from './lib/players';

type Crime = PlayerCrimeInfo['crime'];

// Retail police lines (tables/textdb): "Police! ..." 2100 + the crime's message.
const CRIME_TEXT: Partial<Record<Crime, number>> = {
    weaponSeen: 2100,
    firing: 2101,
    speeding: 2102,
    crashObject: 2103,
    crashCar: 2103,
    crashPedestrian: 2103,
    sidewalk: 2104,
    violence: 2105,
    redLight: 2106,
    carTheft: 2107,
    signDamage: 2108,
    assaultOnPolice: 2109,
    policeCarRammed: 2110,
};
const WANTED_NAMES = [
    'clean',
    'wanted for questioning',
    'wanted for arrest',
    'wanted dead or alive',
];
const FINE = 100;
const BAIL = 250;
const ARREST_RESPAWN_MS = 4000;

// Retail fines cost score and an arrest ends the game. The record is already
// settled when playerArrest arrives; the sentence goes by the highest level
// the player reached on this case.
const caseLevel = new PlayerMap<number>();

type PoliceMessage =
    | { action: 'text'; textId: number }
    | { action: 'wanted'; level: number }
    | { action: 'fine' }
    | { action: 'arrest' };

function sendPolice(player: Player, data: PoliceMessage): void {
    emit(player, 'fr:police', data);
}

events.on('playerCrime', (player, { crime }) => {
    const textId = CRIME_TEXT[crime];

    if (textId !== undefined) {
        sendPolice(player, { action: 'text', textId });
    }
});

events.on('playerWantedChange', (player, { level, previous }) => {
    if (level > 0) {
        caseLevel.set(player.id, Math.max(level, caseLevel.get(player.id) ?? 0));
    }

    sendPolice(player, { action: 'wanted', level });

    if (level === 0 && previous > 0) {
        toast(player, 'The police lost interest in you.', { tone: 'good' });
    }

    if (level === 3 && previous < 3) {
        feed('police', [name(player, COLORS.red), text(' is wanted dead or alive')]);
    }
});

events.on('playerSpawn', (player) =>
    sendPolice(player, { action: 'wanted', level: Police.getRecord(player)?.level ?? 0 }),
);

events.on('playerRespawn', (player) => sendPolice(player, { action: 'wanted', level: 0 }));

// A fine costs cash. An arrest is a jail sentence (jail/); while the jail
// is not built (no map objects yet), it costs bail and a fresh start instead.
events.on('playerArrest', (player, _officer, { kind }) => {
    if (kind === 'fine') {
        const charge = Math.min(player.money, FINE);

        if (charge > 0) {
            player.giveMoney(-charge);
        }

        sendPolice(player, { action: 'fine' });
        toast(player, `Fined ${formatMoney(charge)} by the police.`, { tone: 'bad' });

        return;
    }

    const level = caseLevel.get(player.id) ?? 2;

    caseLevel.delete(player.id);
    const { missionGeneration, spawnGeneration } = player;

    Police.clear(player);
    sendPolice(player, { action: 'arrest' });
    setTimeout(() => {
        // A door or a ride taken during the fade (the record is clear by
        // now) is a new life too, but no way out of the sentence.
        if (
            !player.alive ||
            player.missionGeneration !== missionGeneration ||
            (player.spawnGeneration !== spawnGeneration && !life.isTransit(player))
        ) {
            return;
        }

        if (jail.sentence(player, level)) {
            return;
        }

        const charge = Math.min(player.money, BAIL);

        if (charge > 0) {
            player.giveMoney(-charge);
        }

        toast(player, `Arrested. Bail cost you ${formatMoney(charge)}.`, {
            title: 'Police',
            tone: 'bad',
        });

        feed('police', [name(player), text(' was arrested')]);
        life.restart(player);
    }, ARREST_RESPAWN_MS);
});

commands.register(
    { name: 'wanted', usage: '/wanted', help: 'Show your police record.', category: 'Combat' },
    (player) => {
        const record = Police.getRecord(player);

        if (!record) {
            toast(player, 'No police record while you are off the street.');

            return;
        }

        toast(
            player,
            `You are ${WANTED_NAMES[record.level] ?? 'wanted'}. ${record.officers} officers on you. ` +
                `Crimes: ${record.crimes.join(', ') || 'none'}.`,
            { title: 'Police record', tone: record.level ? 'bad' : 'info' },
        );
    },
);
