// /home and its actions (the Homes tab's buttons send the same text), and
// /homes, the homes for sale nearest the player.
import * as commands from '../lib/commands';
import * as rooms from '../lib/house_rooms';
import { toast } from '../lib/notify';
import { emit, flatDistance, formatMoney } from '../lib/util';
import { here, title } from './catalogue';
import { decorCommand, editCommand } from './decorating';
import { leave } from './entering';
import { homeOf } from './homes';
import { crewCommand, inviteCommand, kickCommand, lockCommand, uninviteCommand } from './keys';
import { buyCommand, sellCommand, showCommand } from './market';
import { insideHouse, moves } from './occupancy';
import { carCommand } from './parking';

const NEAREST_LISTED = 5;

const SUBCOMMANDS: Readonly<Record<string, (player: Player, args: string[]) => void>> = {
    buy: (player) => buyCommand(player),
    show: (player, [id]) => showCommand(player, id),
    sell: (player, [id]) => sellCommand(player, id),
    lock: (player, [id]) => lockCommand(player, id),
    crew: (player, [id]) => crewCommand(player, id),
    invite: (player, [who, id]) => inviteCommand(player, who, id),
    uninvite: (player, [who, id]) => uninviteCommand(player, who, id),
    leave: (player) => {
        if (!insideHouse(player) || moves.isMoving(player)) {
            toast(player, 'You are not in a home.', { tone: 'bad' });

            return;
        }

        leave(player);
    },
    kick: (player) => kickCommand(player),
    edit: (player) => editCommand(player),
    decor: (player, [itemId]) => decorCommand(player, itemId),
    car: (player, [slot]) => carCommand(player, slot),
};

commands.register(
    {
        name: 'home',
        aliases: ['house'],
        usage: '/home [buy | show <id> | sell | lock | crew | invite <player> | uninvite <name> | kick | leave | edit | decor <item> | car]',
        help: 'Your homes: buy at the door, lock, invite, decorate, sell.',
        category: 'City',
    },
    (player, [action, ...args]) => {
        const name = String(action ?? '').toLowerCase();
        const handler = Object.hasOwn(SUBCOMMANDS, name) ? SUBCOMMANDS[name] : null;

        if (!handler) {
            emit(player, 'fr:menu:open', { tab: 'homes' });

            return;
        }

        handler(player, args);
    },
);

commands.register(
    {
        name: 'homes',
        usage: '/homes',
        help: 'Homes for sale near you, on the city map.',
        category: 'City',
    },
    (player) => {
        const from = insideHouse(player)?.door ?? player.position;
        const free = here()
            .filter((house) => !homeOf(house))
            .sort((a, b) => flatDistance(from, a.door) - flatDistance(from, b.door));

        if (!free.length) {
            toast(player, 'Every home on this map is sold.', { seconds: 4 });

            return;
        }

        for (const house of free.slice(0, NEAREST_LISTED)) {
            player.sendMessage(
                `${title(house)} · ${rooms.KINDS[house.tier].title} · ${formatMoney(house.price)} · ${Math.round(flatDistance(from, house.door))} m · /home show ${house.id}`,
                0xd9cdb4,
            );
        }

        showCommand(player, free[0].id);
    },
);
