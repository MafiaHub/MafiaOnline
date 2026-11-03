// Paying a player for something done: cash, the career counters it adds to,
// experience and the city feed's line about it, always in that order, so a
// level-up toast and its feed line come after the money has landed.
import * as profiles from './profiles';
import type { Counter } from './profiles';
import { feed, type FeedPart } from './notify';

/** A career counter bumped by one, or by the count beside it. */
export type CounterBump = Counter | readonly [Counter, number];

export interface Reward {
    money?: number;
    counters?: readonly CounterBump[];
    xp?: number;
    /** The line the experience toast shows. */
    reason?: string;
    /** The city feed's line about it. */
    feed?: { kind: string; parts: FeedPart[] };
}

export function reward(
    player: Player,
    { money = 0, counters = [], xp = 0, reason, feed: line }: Reward,
): void {
    if (money > 0) {
        player.giveMoney(money);
    }

    for (const counter of counters) {
        if (typeof counter === 'string') {
            profiles.bump(player, counter);
        } else {
            profiles.bump(player, counter[0], counter[1]);
        }
    }

    if (xp > 0) {
        profiles.addXp(player, xp, reason);
    }

    if (line) {
        feed(line.kind, line.parts);
    }
}
