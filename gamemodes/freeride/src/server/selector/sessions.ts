// Who is logged in. Until they log in, players are "Guest <id>"
// (lib/accounts.ts reserves the prefix), so nobody can chat or be listed
// under an account's name.
import { PlayerMap } from '../lib/players';
import type { Account } from '../lib/accounts';

export interface Session {
    /** The account's name, lower-cased. */
    account: string;
    /** Logged in, but the one-time look is not chosen yet. */
    needsLook: boolean;
}

export const sessions = new PlayerMap<Session>();

export function makeGuest(player: Player): void {
    const guest = `Guest ${player.id}`;

    if (!sessions.has(player.id) && player.nickname !== guest) {
        player.setNickname(guest);
    }
}

/** Whether another connected player is logged in to the account. */
export function inTown(player: Player, account: Account): boolean {
    return World.getPlayers().some(
        (candidate) =>
            candidate.id !== player.id &&
            sessions.get(candidate.id)?.account === account.name.toLowerCase(),
    );
}

/** Whether anyone connected is logged in to the account of that name. */
export function accountOnline(name: string): boolean {
    return World.getPlayers().some(
        (player) => sessions.get(player.id)?.account === name.toLowerCase(),
    );
}
