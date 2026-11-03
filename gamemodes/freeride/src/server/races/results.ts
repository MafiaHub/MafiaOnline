// The end of a race: prizes for the podium, experience for every finisher,
// route records, and the results card.
import * as log from '../lib/log';
import { toast, card, feed, name, text, COLORS } from '../lib/notify';
import { formatMoney, formatDuration, emit } from '../lib/util';
import { reward } from '../lib/rewards';
import { Cooldown } from '../lib/cooldowns';
import type { Counter } from '../lib/profiles';
import { recordOf, save, setRecord } from './routes';
import { races, racerList, leaveRace, sendHome, ordinal, type Race, type Racer } from './race';

const RECORD_BONUS = 200;
const SOLO_PRIZE = 120;
const SOLO_COOLDOWN_MS = 5 * 60_000;
const SHARES = [0.6, 0.25, 0.15];

const soloPrizes = new Cooldown(SOLO_COOLDOWN_MS);

type Finished = Racer & { finishedMs: number };

function finished(racer: Racer): racer is Finished {
    return racer.finishedMs !== null;
}

function payFinisher(race: Race, racer: Finished, index: number, finishers: Finished[]): void {
    const { player, finishedMs } = racer;
    // A solo run pays only on a built-in route, and once per cooldown.
    const solo = race.entrants === 1;
    const soloPaid = solo && race.route.builtIn && soloPrizes.take(player) === 0;
    const pool = 100 + 120 * race.entrants;
    const prize = solo ? (soloPaid ? SOLO_PRIZE : 0) : Math.round(pool * (SHARES[index] ?? 0));
    const won = index === 0 && race.entrants > 1;
    const counters: Counter[] = won ? ['racesRun', 'racesWon'] : ['racesRun'];

    reward(player, {
        money: prize,
        counters,
        xp: 30 + 25 * Math.max(0, race.entrants - index - 1) + (won ? 60 : 0),
        reason: 'Street race',
    });

    const record = recordOf(race.route.id);

    if (!record || finishedMs < record.ms) {
        // Records count on any run; only one set against rivals pays.
        setRecord(race.route.id, { ms: finishedMs, name: player.nickname, at: Date.now() });
        reward(player, { money: solo ? 0 : RECORD_BONUS, counters: ['raceRecords'] });
        feed('record', [
            name(player, COLORS.gold),
            text(' set the record on '),
            text(race.route.name, COLORS.white),
            text(` · ${formatDuration(finishedMs)}`, COLORS.gold),
        ]);

        toast(
            player,
            solo ? 'New route record!' : `New route record! +${formatMoney(RECORD_BONUS)}`,
            { tone: 'gold' },
        );

        save();
    }

    card(player, 'race', {
        title: `${race.route.name} · results`,
        subtitle: `${ordinal(index + 1)} of ${race.entrants}`,
        rows: finishers
            .slice(0, 5)
            .map((other, place): [string, string] => [
                `${place + 1}. ${other.player.nickname}`,
                formatDuration(other.finishedMs),
            ])
            .concat(prize > 0 ? [['Prize', formatMoney(prize)]] : []),
        tone: 'gold',
        seconds: 15,
    });
}

export function concludeRace(race: Race): void {
    const finishers = racerList(race)
        .filter(finished)
        .sort((a, b) => a.finishedMs - b.finishedMs);

    finishers.forEach((racer, index) => payFinisher(race, racer, index, finishers));

    try {
        race.onConclude?.(
            finishers.map((racer) => ({ player: racer.player, ms: racer.finishedMs })),
            race.entrants,
        );
    } catch (caught) {
        log.error(`Race result handler failed: ${log.describe(caught, true)}`);
    }

    if (finishers.length && race.entrants > 1) {
        feed('race', [
            name(finishers[0].player, COLORS.gold),
            text(` won ${race.route.name} against ${race.entrants - 1} `),
            text(race.entrants === 2 ? 'rival' : 'rivals'),
            text(` · ${formatDuration(finishers[0].finishedMs)}`, COLORS.white),
        ]);
    }

    for (const racer of racerList(race)) {
        if (racer.finishedMs === null) {
            leaveRace(race, racer, 'The race is over. You did not finish.');
        } else {
            race.racers.delete(racer.player.id);
            sendHome(racer);
            emit(racer.player, 'fr:race:state', { active: false });
        }
    }

    races.delete(race.id);
}
