// Police duty: a player can put on the uniform, take a squad car and hunt the
// players the city's police want or who carry a bounty. Taking down a suspect
// pays by wanted level; shooting a citizen costs the badge and a fine. On duty
// the city's own police count the player as one of theirs (Police.setOfficer):
// no crime of theirs is recorded, so they are never chased, fined or arrested,
// and they can call for backup (backup.ts). Between suspects the precinct
// radios in calls of its own: a bag snatcher, an armed robbery (calls.ts).
import * as profiles from '../lib/profiles';
import * as life from '../lib/life';
import * as nametag from '../lib/nametag';
import * as menu from '../lib/menu';
import * as commands from '../lib/commands';
import * as backup from './backup';
import * as calls from './calls';
import { card, compass, feed, name, text, toast, COLORS } from '../lib/notify';
import { distance, formatMoney, onStreet, vehicleById } from '../lib/util';
import { events } from '../lib/lifecycle';
import { defineShift } from '../lib/shift';
import { emptyBut, destroyWhenEmpty } from '../lib/vehicles';
import { spawnBeside } from '../lib/errands';
import { reward } from '../lib/rewards';
import type { Suspect } from './backup';

const UNIFORM = 'Pol01.i3d';
const SQUAD_CAR = 'poli00.i3d';
const KIT: life.Kit = Object.freeze({
    weapons: [
        { weaponId: 6, loaded: 6, reserve: 36 },
        { weaponId: 11, loaded: 8, reserve: 24 },
    ],
    selected: 6,
});
const PAY_PER_LEVEL = 150;
const CITIZEN_FINE = 250;
const XP = 60;
const TICK_MS = 2000;
const HUD = 'duty';
const COMPASS_PRIORITY = 45;

interface Officer {
    carId: number | null;
    pay: number;
}

function bountyOf(player: Player): number {
    const bounty: unknown = player.state.get('fr.bounty');

    return typeof bounty === 'number' ? bounty : 0;
}

function suspect(player: Player): Suspect | null {
    if (!onStreet(player) || duty.has(player)) {
        return null;
    }

    const level = Police.getRecord(player)?.level ?? 0;
    const bounty = bountyOf(player);

    return level > 0 || bounty > 0 ? { player, level, bounty } : null;
}

function suspects(): Suspect[] {
    return World.getPlayers()
        .map(suspect)
        .filter((entry): entry is Suspect => entry !== null);
}

// Every officer's card is made from one list per tick.
let ranked: { at: number; list: Suspect[] } = { at: -1, list: [] };

function suspectsAt(now: number): Suspect[] {
    if (ranked.at !== now) {
        ranked = { at: now, list: suspects() };
    }

    return ranked.list;
}

const duty = defineShift<Officer>({
    kind: 'duty',
    activity: 'Police duty',
    title: 'Police duty',
    hud: HUD,
    compassPriority: COMPASS_PRIORITY,
    flag: { key: 'fr.duty', value: true },
    uniform: UNIFORM,
    kit: KIT,
    // Another module took the player off duty (the jail): badge, car and
    // backup go back.
    interrupted: 'Your shift is over.',
    missionChange: 'Your shift ended with the city reloading.',
    tickMs: TICK_MS,

    begin(player) {
        if ((Police.getRecord(player)?.level ?? 0) > 0 || bountyOf(player)) {
            return 'The precinct will not swear in a wanted man.';
        }

        return { carId: null, pay: 0 };
    },

    started(player, officer) {
        Police.setOfficer(player, true);
        life.setOverride(player, { kit: KIT });
        nametag.refresh(player);
        officer.carId =
            spawnBeside(player, SQUAD_CAR, { owner: player, radar: 0x6fa8ff })?.id ?? null;

        backup.start(player);
        feed('police', [name(player, COLORS.blue), text(' went on police duty')]);
        toast(
            player,
            "On duty. The city's police take you for one of their own. Wanted players and bounty targets show on your compass; " +
                'call boxes on your map put backup a phone call away (U at a booth, or /backup in your squad car). ' +
                'Leave citizens alone. /duty to clock off.',
            { title: 'Police duty', tone: 'gold', seconds: 10 },
        );
    },

    beforeTick: (now) => calls.update(now),

    tick(officer, state, now) {
        if (!officer.spawned) {
            return;
        }

        const near = suspectsAt(now)
            .map((entry) => ({ ...entry, gap: distance(officer.position, entry.player.position) }))
            .sort((a, b) => a.gap - b.gap);
        // A wanted player comes first; the precinct's own call otherwise.
        const call = calls.callFor(officer);

        calls.promptOfficer(officer);
        compass(
            officer,
            HUD,
            COMPASS_PRIORITY,
            near[0] ? { playerId: near[0].player.id } : call ? { position: call.position } : null,
        );

        card(officer, HUD, {
            title: 'Police duty',
            subtitle: near.length
                ? 'Nearest suspect on your compass'
                : call
                  ? `${call.label}: on your compass`
                  : 'Quiet streets. Patrol.',
            rows: [
                ...(call ? [[call.label, `${Math.round(call.gap)} m`] as [string, string]] : []),
                ...near
                    .slice(0, 3)
                    .map((entry): [string, string] => [
                        entry.player.nickname,
                        `${entry.level ? `wanted ${entry.level}` : formatMoney(entry.bounty)} · ${Math.round(entry.gap)} m`,
                    ]),
                ['Backup', backup.status(officer) ?? ''],
                ['Earned this shift', formatMoney(state.pay)],
            ],
            tone: 'info',
        });
    },

    end(player, officer, how) {
        Police.setOfficer(player, false);
        backup.stop(player);

        if (how.online) {
            calls.clearPrompt(player);
        }

        life.clearOverride(player);

        // The nametag goes by the flag, which the shift clears only after this.
        if (how.online) {
            profiles.setPublic(player, 'fr.duty', null);
            nametag.refresh(player);
        }

        const car = vehicleById(officer.carId);

        if (car && emptyBut(car, player)) {
            destroyWhenEmpty(car.id, 3000);
        }
    },
});

events.on('playerDeath', (victim, killer) => {
    if (!(killer instanceof Player) || killer.id === victim.id) {
        return;
    }

    const officer = duty.get(killer);

    if (!officer) {
        return;
    }

    const level = Police.getRecord(victim)?.level ?? 0;

    if (level === 0 && !bountyOf(victim)) {
        const fine = Math.min(killer.money, CITIZEN_FINE);

        killer.giveMoney(-fine);
        duty.stop(killer, `You shot a citizen. Badge and ${formatMoney(fine)} taken.`, 'bad');
        feed('police', [name(killer, COLORS.red), text(' lost their badge')]);

        return;
    }

    const pay = PAY_PER_LEVEL * Math.max(1, level);

    reward(killer, {
        money: pay,
        counters: ['arrests'],
        xp: XP,
        reason: 'Took down a suspect',
        feed: {
            kind: 'police',
            parts: [
                text('Officer ', COLORS.blue),
                name(killer, COLORS.blue),
                text(' took down '),
                name(victim),
            ],
        },
    });

    officer.pay += pay;
    Police.clear(victim);
});

commands.register(
    {
        name: 'duty',
        aliases: ['cop', 'police'],
        usage: '/duty',
        help: 'Go on or off police duty.',
        category: 'City',
    },
    (player) => {
        if (duty.has(player)) {
            duty.stop(player, 'Off duty. Uniform and squad car returned.');
        } else {
            duty.start(player);
        }
    },
);

menu.provide('duty', (player) => ({
    onDuty: duty.has(player),
    officers: duty.size,
    suspects: suspects().length,
}));

calls.configure({
    onDuty: (player) => duty.has(player),
    earn(player, amount) {
        const officer = duty.get(player);

        if (officer) {
            officer.pay += amount;
        }
    },
});

backup.configure({
    onDuty: (player) => duty.has(player),
    carOf: (player) => vehicleById(duty.get(player)?.carId),
    suspects,
});

export const onDuty = (player: Player): boolean => duty.has(player);
