// The one-time look. Only after the very first registration does a player
// pick one of the stock characters, standing at the district they will start
// in; choosing it puts them on the street.
import * as profiles from '../lib/profiles';
import { emit, onClient } from '../lib/util';
import { fields } from '../lib/payload';
import { CHARACTERS, type Character } from './characters';
import { enter, lookAnchor, type Anchor } from './entry';
import { sessions } from './sessions';

/** What the login card needs to offer the look. */
export interface LookOffer {
    characters: readonly Character[];
    anchor: Anchor | null;
}

export function lookOffer(player: Player): LookOffer {
    return { characters: CHARACTERS, anchor: lookAnchor(player) };
}

onClient('fr:login:look', (player, request: unknown) => {
    const { model, generation } = fields(request);
    const session = sessions.get(player.id);
    const character = CHARACTERS.find((entry) => entry.file === model);

    if (
        !session?.needsLook ||
        !character ||
        player.spawned ||
        generation !== World.getMissionGeneration()
    ) {
        return;
    }

    profiles.setField(player, 'model', character.file);
    session.needsLook = false;

    if (!enter(player)) {
        session.needsLook = true;
        profiles.setField(player, 'model', null);
        emit(player, 'fr:login:error', {
            message: 'That entrance is unavailable. Try again in a moment.',
        });
    }
});
