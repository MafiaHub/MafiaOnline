// One player taking on another: naming them, putting the challenge to them
// (lib/invites.ts) and holding the stakes both put down.
import * as invites from './invites';
import { toast } from './notify';
import { findPlayer } from './util';

export const NOBODY = 'Nobody by that name is in town.';

/** Another player by name (see findPlayer), or null with a toast when there is none or it is the player. */
export function opponent(player: Player, query: unknown): Player | null {
    const target = findPlayer(query);

    if (!target || target.id === player.id) {
        toast(player, NOBODY, { tone: 'bad' });

        return null;
    }

    return target;
}

/** Puts the offer to the target and tells its sender `sent`. */
export function challenge(target: Player, offer: invites.InviteOffer, sent: string): void {
    invites.offer(target, offer);
    toast(offer.from, sent);
}

/**
 * Takes `amount` from each player in turn. When one cannot pay, those who
 * already did get theirs back and that player is returned; null when all paid.
 */
export function collectStakes(players: readonly Player[], amount: number): Player | null {
    const paid: Player[] = [];

    for (const player of players) {
        if (!player.trySpendMoney(amount)) {
            for (const payer of paid) {
                payer.giveMoney(amount);
            }

            return player;
        }

        paid.push(player);
    }

    return null;
}
