// Waits per player between uses of something: a command, a request, a
// prize. Each player's last use is dropped once they disconnect.
import { PlayerMap } from './players';

/** When each player last used something that then rests for `ms`. */
export class Cooldown {
    private readonly last = new PlayerMap<number>();

    constructor(readonly ms: number) {}

    /** How long until the player may again, in ms; 0 when they may now. */
    remaining(player: Player, now = Date.now()): number {
        const last = this.last.get(player.id);

        return last === undefined ? 0 : Math.max(0, last + this.ms - now);
    }

    /** Starts the wait from now. */
    start(player: Player, now = Date.now()): void {
        this.last.set(player.id, now);
    }

    /** 0, starting the wait, when the player may go now; else what is left, in ms. */
    take(player: Player): number {
        const now = Date.now();
        const left = this.remaining(player, now);

        if (left === 0) {
            this.start(player, now);
        }

        return left;
    }
}
