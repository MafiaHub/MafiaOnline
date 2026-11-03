// The "fr" server console command. Its actions live with what they act on:
// "fr as" in dev.ts, "fr event" in world_events.ts; the rest are here.
import * as profiles from '../lib/profiles';
import * as devmode from '../lib/devmode';
import * as admin from '../lib/admin';
import * as serverConsole from '../lib/console';
import * as log from '../lib/log';
import { toastAll } from '../lib/notify';
import { findPlayer, formatMoney } from '../lib/util';

serverConsole.command('fr', () =>
    log.info(
        'Console: fr save | fr money <player> <amount> | fr say <text> | fr admin add|remove <player> | ' +
            'fr admin list | fr as <player> <dev action> | fr dev on|off | fr event <supply|hotcar>',
    ),
);

serverConsole.action('fr', 'save', () => {
    profiles.save();
    log.info('Player records saved');
});

serverConsole.action('fr', 'money', ([who, dollars]) => {
    if (who === undefined || dollars === undefined) {
        return false;
    }

    const target = findPlayer(who);
    const amount = Number(dollars);

    if (target && Number.isInteger(amount) && target.giveMoney(amount)) {
        log.info(`${target.nickname} now has ${formatMoney(target.money)}`);
    } else {
        log.info('Usage: fr money <player> <whole dollars>');
    }

    return true;
});

serverConsole.action('fr', 'admin', ([mode, ...names]) => {
    if ((mode === 'add' || mode === 'remove') && names[0]) {
        // A connected player's own record, or every saved record by that name.
        const query = names.join(' ');
        const target = findPlayer(query);
        const own = target && profiles.keyOf(target);
        const keys = own ? [own] : profiles.keysNamed(query);

        if (!keys.length) {
            const connected = World.getPlayers().map((player) => player.nickname);

            log.info(
                `No player record named "${query}"; connected: ${connected.join(', ') || 'nobody'}`,
            );

            return;
        }

        for (const key of keys) {
            admin.set(key, mode === 'add');
        }

        log.info(`${keys.join(', ')} ${mode === 'add' ? 'now admin' : 'no longer admin'}`);

        return;
    }

    log.info(`Admins: ${admin.list().join(', ') || 'none'}`);
});

serverConsole.action('fr', 'dev', ([mode]) => {
    if (mode !== 'on' && mode !== 'off') {
        return false;
    }

    devmode.set(mode === 'on');
    log.info(`Development commands ${devmode.isOn() ? 'on' : 'off'}`);

    return true;
});

serverConsole.action('fr', 'say', (words) => {
    if (!words.length) {
        return false;
    }

    toastAll(words.join(' '), { title: 'City Hall', tone: 'gold', seconds: 8 });

    return true;
});
