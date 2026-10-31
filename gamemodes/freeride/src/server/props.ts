// City upkeep. Phone booths, hydrants, trash bins, barrels and traffic signs
// stay knocked down and boxes stay where they were pushed, for every player
// alike, until the server repairs them. Here the city crew comes round a few
// minutes after the first damage; /props shows the damage and /repairprops
// calls the crew at once. Anyone may call the crew, at most once a minute.
import * as commands from './lib/commands';
import { events, every } from './lib/lifecycle';
import { toast, toastAll } from './lib/notify';
import { plural } from './lib/util';

const REPAIR_AFTER_MS = 5 * 60_000;
const CHECK_MS = 15_000;
const CALL_COOLDOWN_MS = 60_000;

// When the damage since the last repair started, or null while all is whole.
let damagedSince: number | null = null;
// When a player last called the crew.
let lastCall = 0;

function describe({ broken, moved }: PropDamageInfo): string {
    return `${plural(broken, 'broken prop')}, ${moved} moved`;
}

function repair(reason: string): number {
    const restored = World.repairProps();

    damagedSince = null;

    if (restored > 0) {
        toastAll(`The city crew ${reason}: ${plural(restored, 'prop')} back in place.`, {
            title: 'City crew',
        });
    }

    return restored;
}

every(CHECK_MS, () => {
    const damage = World.getPropDamage();

    if (damage.broken + damage.moved === 0) {
        damagedSince = null;

        return;
    }

    damagedSince ??= Date.now();

    if (Date.now() - damagedSince >= REPAIR_AFTER_MS) {
        repair('came round');
    }
});

// A new mission starts with every prop whole.
events.on('missionChange', () => {
    damagedSince = null;
});

commands.register(
    {
        name: 'props',
        usage: '/props',
        help: 'How much of the city lies broken.',
        category: 'Showcase',
    },
    (player) => {
        const due =
            damagedSince === null
                ? ''
                : `; the crew comes in ${Math.max(0, Math.ceil((damagedSince + REPAIR_AFTER_MS - Date.now()) / 60_000))} min`;

        toast(player, `City props: ${describe(World.getPropDamage())}${due}.`);
    },
);

commands.register(
    {
        name: 'repairprops',
        usage: '/repairprops',
        help: 'Call the city crew to fix every broken prop.',
        category: 'Showcase',
    },
    (player) => {
        const wait = lastCall + CALL_COOLDOWN_MS - Date.now();

        if (wait > 0) {
            toast(
                player,
                `The city crew was just called; try again in ${Math.ceil(wait / 1000)} s.`,
                {
                    tone: 'bad',
                },
            );

            return;
        }

        lastCall = Date.now();

        if (repair(`was called by ${player.nickname}`) === 0) {
            toast(player, 'Nothing to repair: every prop is in place.');
        }
    },
);
