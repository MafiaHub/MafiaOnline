// The local "fr:login" event: server/selector announces each login, and the
// modules that dress a player for the city (crews, the join feed, drunkenness)
// listen for it. A resource restart hands logins over and announces them
// again, marked restored.
import { events } from './lifecycle';

const EVENT = 'fr:login';

export interface LoginInfo {
    /** Whether the account's record existed before. */
    returning: boolean;
    account: string;
    /** A new account took over a record kept before accounts. */
    claimed?: boolean;
    /** Handed over from before a resource restart, not a new login. */
    restored?: boolean;
}

export function announceLogin(player: Player, info: LoginInfo): void {
    void events.emitLocal(EVENT, player, info);
}

export function onLogin(handler: (player: Player, info: LoginInfo) => void): void {
    events.onLocal(EVENT, (player, info) => {
        // Only announceLogin emits it.
        if (player instanceof Player) {
            handler(player, info as LoginInfo);
        }
    });
}
