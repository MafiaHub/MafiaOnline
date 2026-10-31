// Who is in town and talking to them: private messages, the player list and
// watching another player's camera.
import * as invites from '../lib/invites';
import * as menu from '../lib/menu';
import * as commands from '../lib/commands';
import { toast } from '../lib/notify';
import { findPlayer } from '../lib/util';
import { opponent } from '../lib/challenges';

commands.register(
    {
        name: 'pm',
        aliases: ['msg', 'w'],
        usage: '/pm <player> <message>',
        help: 'Send a private message.',
        category: 'Social',
    },
    (player, args) => {
        const target = findPlayer(args[0]);
        const message = args.slice(1).join(' ').trim().slice(0, 120);

        if (!target || !message) {
            toast(player, 'Use /pm <player> <message>.', { tone: 'bad' });

            return;
        }

        target.sendMessage(`[from ${player.nickname}] ${message}`, 0xc9a0e8);
        player.sendMessage(`[to ${target.nickname}] ${message}`, 0x9d86b0);
    },
);

commands.register(
    {
        name: 'spectate',
        aliases: ['follow', 'watch'],
        usage: '/spectate <player|off>',
        help: "Watch through another player's camera.",
        category: 'Social',
    },
    (player, args) => {
        const query = args.join(' ').trim();

        if (!query || query.toLowerCase() === 'off') {
            if (player.setCameraTarget(null)) {
                toast(player, 'Back to your own eyes.');
            }

            return;
        }

        const target = opponent(player, query);

        if (!target) {
            return;
        }

        toast(
            player,
            player.setCameraTarget(target)
                ? `Watching ${target.nickname}. /spectate off to stop.`
                : `Cannot watch ${target.nickname} right now.`,
        );
    },
);

commands.register(
    {
        name: 'players',
        aliases: ['who'],
        usage: '/players',
        help: 'List who is in town.',
        category: 'Social',
    },
    (player) => {
        const list = World.getPlayers().map((other) => `${other.nickname} (${other.ping} ms)`);

        player.sendMessage(`${list.length} in town: ${list.join(', ')}`, 0xd9cdb4);
    },
);

menu.provide(
    'players',
    (viewer) => ({
        list: World.getPlayers().map((player) => {
            const state = (key: string): unknown => player.state.get(key) ?? null;

            return {
                id: player.id,
                name: player.nickname,
                you: player.id === viewer.id,
                ping: player.ping,
                level: (player.state.get('fr.level') as unknown) ?? 1,
                rank: (player.state.get('fr.rank') as unknown) ?? '',
                crew: state('fr.crew'),
                activity: state('fr.activity'),
                bounty: state('fr.bounty'),
                spawned: player.spawned,
            };
        }),
        invites: invites.listFor(viewer),
    }),
    ['city', 'players'],
);
