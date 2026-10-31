// The F2 city menu's plumbing: the sections each module provides
// (lib/menu.ts), the command table, the player's own progress, and the
// commands its buttons send. The leaderboards are this module's section.
import * as commands from '../lib/commands';
import * as profiles from '../lib/profiles';
import * as menu from '../lib/menu';
import { emit, onClient } from '../lib/util';
import { Cooldown } from '../lib/cooldowns';
import { fields } from '../lib/payload';

const MENU_COMMAND_INTERVAL_MS = 300;
const MENU_REQUEST_INTERVAL_MS = 800;
// Sorting every record ever saved: a minute old is fresh enough.
const LEADERBOARD_CACHE_MS = 60_000;

interface Leaderboards {
    level: profiles.LeaderboardRow[];
    money: profiles.LeaderboardRow[];
    kills: profiles.LeaderboardRow[];
    races: profiles.LeaderboardRow[];
}

const menuRequests = new Cooldown(MENU_REQUEST_INTERVAL_MS);
// Menu buttons skip the chat rate limit, so they get their own.
const menuCommands = new Cooldown(MENU_COMMAND_INTERVAL_MS);
let leaderboardCache: { at: number; board: Leaderboards } | null = null;

onClient('fr:menu:request', (player, request: unknown) => {
    if (menuRequests.take(player) > 0) {
        return;
    }

    const { tab } = fields(request);

    emit(player, 'fr:menu:data', {
        ...menu.collect(player, typeof tab === 'string' ? tab : null),
        commands: commands.list(),
        you: {
            name: player.nickname,
            money: player.money,
            ...profiles.progress(profiles.get(player) ?? { xp: 0 }),
        },
    });
});

onClient('fr:menu:command', (player, request: unknown) => {
    if (menuCommands.take(player) === 0) {
        commands.runText(player, fields(request).text);
    }
});

menu.provide(
    'leaderboard',
    () => {
        if (!leaderboardCache || Date.now() - leaderboardCache.at > LEADERBOARD_CACHE_MS) {
            leaderboardCache = {
                at: Date.now(),
                board: {
                    level: profiles.leaderboard('level'),
                    money: profiles.leaderboard('money'),
                    kills: profiles.leaderboard('kills'),
                    races: profiles.leaderboard('racesWon'),
                },
            };
        }

        return leaderboardCache.board;
    },
    ['leaders'],
);
