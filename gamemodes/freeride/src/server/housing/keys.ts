// Who else may come in: the owner locks or opens the home, lets their crew
// come and go, gives friends a key and takes it back, and shows people out.
import { toast } from '../lib/notify';
import { findPlayer } from '../lib/util';
import { title } from './catalogue';
import { evict, leave } from './entering';
import { crewTagOf, homeOf, isOwner, keyOf, save } from './homes';
import { publish } from './markers';
import { insideHouse, mine, occupants } from './occupancy';

/** /home lock: locks or opens the home. */
export function lockCommand(player: Player, id: string | undefined): void {
    const house = mine(player, id);
    const home = house && homeOf(house);

    if (!house || !home) {
        return;
    }

    home.locked = !home.locked;
    save();

    if (home.locked) {
        evict(house, `${player.nickname} locked up and showed you out.`);
    }

    toast(
        player,
        home.locked
            ? 'Locked: only you, your guests and your crew (if you let them) come in.'
            : 'Open house: anyone may walk in.',
        { seconds: 4 },
    );
}

/** /home crew: lets the owner's crew in, or no longer. */
export function crewCommand(player: Player, id: string | undefined): void {
    const house = mine(player, id);
    const home = house && homeOf(house);

    if (!house || !home) {
        return;
    }

    const tag = crewTagOf(player);

    if (!home.crew && !tag) {
        toast(player, 'You are not in a crew.', { tone: 'bad' });

        return;
    }

    home.crew = home.crew ? null : tag;
    save();

    if (!home.crew) {
        evict(house, `${player.nickname} showed the crew out.`);
    }

    toast(
        player,
        home.crew
            ? `Your crew [${home.crew}] may come and go.`
            : 'Your crew needs an invitation again.',
        { seconds: 4 },
    );
}

/** /home invite <player>: a key for them. */
export function inviteCommand(
    player: Player,
    who: string | undefined,
    id: string | undefined,
): void {
    const guest = findPlayer(who);
    const house = mine(player, id);
    const home = house && homeOf(house);

    if (!house || !home) {
        return;
    }

    const guestKey = guest ? keyOf(guest) : null;

    if (!guest || guest.id === player.id || !guestKey) {
        toast(player, 'Use /home invite <player>.', { tone: 'bad' });

        return;
    }

    home.guests = { ...home.guests, [guestKey]: guest.nickname };
    save();
    toast(player, `${guest.nickname} may come into ${title(house)} now.`, {
        tone: 'good',
        seconds: 4,
    });

    toast(
        guest,
        `${player.nickname} invited you to ${title(house)}. It is marked on your city map (TAB).`,
        { tone: 'good', seconds: 7 },
    );

    publish(guest);
}

/** /home uninvite <name>: takes a guest's key back. */
export function uninviteCommand(
    player: Player,
    who: string | undefined,
    id: string | undefined,
): void {
    const house = mine(player, id);
    const home = house && homeOf(house);

    if (!house || !home) {
        return;
    }

    const wanted = String(who ?? '').toLowerCase();
    const key = Object.keys(home.guests).find(
        (guestKey) => String(home.guests[guestKey]).toLowerCase() === wanted,
    );

    if (!key) {
        toast(player, 'No guest by that name.', { tone: 'bad' });

        return;
    }

    delete home.guests[key];
    save();
    evict(house, `${player.nickname} took your key back.`);
    toast(player, `${who} is no longer a guest.`, { seconds: 3 });
}

/** /home kick: shows everyone else out of the home the owner stands in. */
export function kickCommand(player: Player): void {
    const house = insideHouse(player);

    if (!house || !isOwner(player, house)) {
        toast(player, 'Stand in your own home to show people out.', { tone: 'bad' });

        return;
    }

    const visitors = occupants(house).filter((visitor) => visitor.id !== player.id);

    for (const visitor of visitors) {
        toast(visitor, `${player.nickname} showed you out.`, { seconds: 3 });
        leave(visitor);
    }

    toast(player, visitors.length ? 'Everyone else has gone.' : 'Nobody else is here.', {
        seconds: 3,
    });
}
