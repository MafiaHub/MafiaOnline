// The server's accounts (lib/accounts.ts) kept in the database, /logout, and
// the "account" console command.
import * as store from '../lib/store';
import * as profiles from '../lib/profiles';
import * as commands from '../lib/commands';
import * as serverConsole from '../lib/console';
import * as log from '../lib/log';
import { createAccounts, type AccountsDocument, type LoginResult } from '../lib/accounts';
import { toast } from '../lib/notify';
import { plural } from '../lib/util';
import { Delay } from '../lib/timers';
import { accountOnline, sessions } from './sessions';

// Every login rewrites the whole accounts document (a token is replaced):
// after a restart the whole server logs in at once, so the writes within a
// second come to one. The resource's stop writes what is still waiting.
const ACCOUNTS_SAVE_DELAY_MS = 1000;
const accountsSave = new Delay();
let pendingAccounts: AccountsDocument | null = null;

export function flushAccounts(): void {
    accountsSave.cancel();

    if (pendingAccounts) {
        store.save('accounts', pendingAccounts);
    }

    pendingAccounts = null;
}

export const accounts = createAccounts({
    load: () => store.load<AccountsDocument>('accounts'),
    save: (document) => {
        pendingAccounts = document;

        if (!accountsSave.pending) {
            accountsSave.start(ACCOUNTS_SAVE_DELAY_MS, flushAccounts);
        }
    },
});

commands.register(
    {
        name: 'logout',
        usage: '/logout',
        help: 'Sign out every computer that remembers your login.',
        category: 'City',
    },
    (player) => {
        const session = sessions.get(player.id);

        if (!session) {
            return;
        }

        const count = accounts.revokeAll(session.account);

        toast(
            player,
            count
                ? `Signed out ${plural(count, 'remembered login')}. Next time you log in with your password.`
                : 'No computer remembers your login.',
            { title: 'Account', tone: 'good' },
        );
    },
);

serverConsole.command('account', () =>
    log.info(
        'Console: account list | account revoke <name> | account password <name> <new password> | ' +
            'account claim <name> <record key>',
    ),
);

serverConsole.action('account', 'list', () => {
    for (const entry of accounts.list()) {
        log.info(
            `${entry.name}: record ${entry.key}, ${entry.tokens} remembered, ` +
                `last login ${new Date(entry.lastLogin).toISOString()}`,
        );
    }
});

serverConsole.action('account', 'revoke', ([name]) => {
    if (!name) {
        return false;
    }

    log.info(`${accounts.revokeAll(name)} remembered logins of ${name} revoked`);

    return true;
});

serverConsole.action('account', 'password', ([name, ...words]) => {
    if (!name || !words.length) {
        return false;
    }

    return accounts.setPassword(name, words.join(' ')).then((result) => {
        log.info(
            result.ok
                ? `Password of ${name} changed; its remembered logins were revoked`
                : result.message,
        );
    });
});

serverConsole.action('account', 'claim', ([name, key]) => {
    if (!name || !key) {
        return false;
    }

    const result: LoginResult = accountOnline(name)
        ? { ok: false, message: `${name} is in town; try again once they have left.` }
        : profiles.exists(key)
          ? accounts.assignRecord(name, key)
          : { ok: false, message: `No player record ${key}.` };

    log.info(result.ok ? `${result.account.name} now owns record ${key}` : result.message);

    return true;
});
