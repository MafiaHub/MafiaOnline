// Per-player state. Nearly every module keeps maps and sets keyed by player id
// and has to drop the entry when the player leaves (and often when the mission
// changes); these do that themselves.
import { afterDisconnect, onMissionChange } from './lifecycle';

export interface PlayerStateOptions {
    /** Also empty the collection when the mission changes. */
    clearOnMissionChange?: boolean;
}

/**
 * State per player id, dropped once the player has disconnected: after every
 * module's own disconnect handler, so those can still read it.
 */
export class PlayerMap<T> extends Map<number, T> {
    constructor(options: PlayerStateOptions = {}) {
        super();
        afterDisconnect((player) => this.delete(player.id));

        if (options.clearOnMissionChange) {
            onMissionChange(() => this.clear());
        }
    }

    of(player: Player): T | undefined {
        return this.get(player.id);
    }
}

/** A set of player ids with the same lifetime rules as PlayerMap. */
export class PlayerSet extends Set<number> {
    constructor(options: PlayerStateOptions = {}) {
        super();
        afterDisconnect((player) => this.delete(player.id));

        if (options.clearOnMissionChange) {
            onMissionChange(() => this.clear());
        }
    }
}

/** Last payload sent to each player, so an unchanged one is not sent again. */
export class SentCache extends PlayerMap<string> {
    /** True (and remembered) when `payload` differs from what the player has. */
    changed(player: Player, payload: string): boolean {
        if (this.get(player.id) === payload) {
            return false;
        }

        this.set(player.id, payload);

        return true;
    }
}
