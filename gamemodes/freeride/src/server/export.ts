// The export list. Lucas Bertone wants three particular models, and only
// cars taken from the city's traffic count; a bought or admin car is
// worthless to him. The list comes from the models actually driving around
// this server and changes once all three are in, or after an hour. Each map
// names its own yard in lib/maps.ts; on Lost Heaven Extended it is his garage.
import * as maps from './lib/maps';
import * as menu from './lib/menu';
import * as commands from './lib/commands';
import { toast, toastAll, card, name, text, COLORS } from './lib/notify';
import { flatDistance, formatMoney, formatMinutes, playable, onStart } from './lib/util';
import { every } from './lib/lifecycle';
import { PlayerMap } from './lib/players';
import { beacon, clearHud, lead, stoppedAt } from './lib/errands';
import { reward } from './lib/rewards';

const HUD = 'export';
const COMPASS_PRIORITY = 50;
const YARD_RADIUS = 12;
const STOP_SPEED = 3;
const LIST_SIZE = 3;
const LIST_LIFETIME_MS = 60 * 60_000;
const PAY = 400;
const COMPLETE_BONUS = 600;
const XP = 80;
const TICK_MS = 1000;
// Used until enough traffic has been seen to choose from it.
const FALLBACK = [
    'fordtto00.i3d',
    'forvtud00.i3d',
    'chev00.i3d',
    'hudfor00.i3d',
    'pontfor00.i3d',
    'buikfor00.i3d',
];

interface Wanted {
    model: string;
    deliveredBy: string | null;
}

interface ExportList {
    models: Wanted[];
    expiresAt: number;
}

const seen = new Set<string>();
let list: ExportList | null = null;
// The car each player was last told Lucas wants.
const hinted = new PlayerMap<number>({ clearOnMissionChange: true });

function newList(): ExportList {
    const pool = [...(seen.size >= LIST_SIZE * 2 ? seen : new Set([...seen, ...FALLBACK]))];
    const models: string[] = [];

    while (models.length < LIST_SIZE && pool.length) {
        models.push(pool.splice(Math.floor(Math.random() * pool.length), 1)[0]);
    }

    list = {
        models: models.map((model) => ({ model, deliveredBy: null })),
        expiresAt: Date.now() + LIST_LIFETIME_MS,
    };

    hinted.clear();

    return list;
}

function currentList(): ExportList {
    return list ?? newList();
}

function yard(): maps.Landmark | null {
    return maps.current()?.exportYard ?? null;
}

function shortName(model: string): string {
    return model.replace(/00\.i3d$/i, '');
}

function wanted(vehicle: Vehicle): Wanted | null {
    if (!vehicle.traffic || vehicle.trafficKind !== 'civil') {
        return null;
    }

    const model = vehicle.model.toLowerCase();

    return (
        currentList().models.find((entry) => entry.model === model && !entry.deliveredBy) ?? null
    );
}

function clearHint(player: Player): void {
    if (hinted.delete(player.id)) {
        clearHud(player, HUD, COMPASS_PRIORITY);
    }
}

function deliver(player: Player, vehicle: Vehicle, entry: Wanted, target: maps.Landmark): void {
    entry.deliveredBy = player.nickname;
    clearHint(player);
    reward(player, {
        money: PAY,
        counters: ['exports'],
        xp: XP,
        reason: 'Car for Lucas',
        feed: {
            kind: 'export',
            parts: [
                name(player, COLORS.gold),
                text(' delivered a '),
                text(shortName(vehicle.model), COLORS.white),
                text(` to ${target.name}`),
            ],
        },
    });

    toast(player, `Lucas looks it over and nods. ${formatMoney(PAY)}.`, {
        title: 'Export',
        tone: 'good',
    });

    player.removeFromVehicle();
    setTimeout(() => vehicle.destroy(), 1500);

    if (currentList().models.every((model) => model.deliveredBy)) {
        player.giveMoney(COMPLETE_BONUS);
        toastAll(`${player.nickname} closed Lucas's list. A new one is up.`, {
            title: 'Export',
            tone: 'gold',
        });

        newList();
    }
}

function tick(): void {
    for (const vehicle of World.getVehicles()) {
        if (vehicle.traffic && vehicle.trafficKind === 'civil') {
            seen.add(vehicle.model.toLowerCase());
        }
    }

    if (!list || Date.now() > list.expiresAt) {
        newList();
    }

    const target = yard();

    if (!target) {
        return;
    }

    for (const player of World.getPlayers()) {
        const vehicle = playable(player) && player.getSeat() === 0 ? player.getVehicle() : null;
        const entry = vehicle ? wanted(vehicle) : null;

        if (!vehicle || !entry) {
            clearHint(player);
            continue;
        }

        if (stoppedAt(vehicle, target.position, YARD_RADIUS, STOP_SPEED)) {
            deliver(player, vehicle, entry, target);
            continue;
        }

        if (hinted.get(player.id) !== vehicle.id) {
            hinted.set(player.id, vehicle.id);
            lead(
                player,
                HUD,
                COMPASS_PRIORITY,
                beacon('yard', target.position, target.name.toUpperCase(), COLORS.gold),
            );

            toast(player, `Lucas wants this one. Bring it to ${target.name} in one piece.`, {
                title: 'Export list',
                tone: 'gold',
            });
        }

        card(player, HUD, {
            title: 'Export',
            subtitle: `Deliver to ${target.name}`,
            rows: [
                ['Distance', `${Math.round(flatDistance(vehicle.position, target.position))} m`],
                ['Pay', formatMoney(PAY)],
            ],
            tone: 'gold',
        });
    }
}

commands.register(
    {
        name: 'export',
        aliases: ['lucas'],
        usage: '/export',
        help: 'Which traffic cars Lucas wants this hour.',
        category: 'City',
    },
    (player) => {
        const current = currentList();
        const open = current.models
            .filter((entry) => !entry.deliveredBy)
            .map((entry) => shortName(entry.model));

        toast(
            player,
            `Lucas wants: ${open.join(', ')}. Take them from traffic; your own car does not count. ` +
                `New list in ${formatMinutes(current.expiresAt - Date.now())}.`,
            { title: 'Export list', seconds: 9 },
        );
    },
);

menu.provide(
    'export',
    () =>
        list && {
            models: list.models,
            minutesLeft: Math.ceil((list.expiresAt - Date.now()) / 60_000),
            pay: PAY,
            yard: yard()?.name ?? null,
        },
);

onStart(newList);
every(TICK_MS, tick);
