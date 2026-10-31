// Whose plants and loads are whose: what a player's record planted is theirs,
// and their crew's. Owners hear of their crop when they are online.
import * as profiles from '../lib/profiles';
import { toast, type Tone } from '../lib/notify';
import type { CrewTag } from '../lib/nametag';

export const TITLE = 'Tobacco farm';
const NOTICE_EVERY_MS = 45_000;

/** Something a player record owns, with the crew it shares it with. */
export interface Owned {
    ownerKey: string;
    ownerCrew: string | null;
}

// Owner key and kind of news -> when the owner last heard it.
const notices = new Map<string, number>();

export function crewTag(player: Player): string | null {
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');

    return crew?.tag ?? null;
}

/** Whether it is the player's own, or their crew's (crewmates share the farm). */
export function mine(player: Player, owned: Owned): boolean {
    const tag = owned.ownerCrew;

    return profiles.keyOf(player) === owned.ownerKey || (Boolean(tag) && crewTag(player) === tag);
}

export function ownerOnline(key: string): Player | null {
    return World.getPlayers().find((player) => profiles.keyOf(player) === key) ?? null;
}

/** A toast to an owner, at most once in a while for each kind of news. */
export function notice(key: string, kind: string, message: string, tone: Tone = 'gold'): void {
    const player = ownerOnline(key);

    if (!player) {
        return;
    }

    const id = `${key}:${kind}`;

    if (Date.now() - (notices.get(id) ?? 0) < NOTICE_EVERY_MS) {
        return;
    }

    notices.set(id, Date.now());
    toast(player, message, { title: TITLE, tone });
}
