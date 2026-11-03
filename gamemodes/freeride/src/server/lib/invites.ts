// Pending requests between players: duels, rides, crew invitations. The
// target answers with /accept or /decline, from chat or the city menu.
import * as commands from './commands';
import { toast, card } from './notify';
import { playerById } from './util';
import { events } from './lifecycle';
import { PlayerMap } from './players';

const INVITE_MS = 30_000;

/** Runs with the player who answered and the one who asked. */
export type InviteAnswer = (target: Player, from: Player) => void;

export interface InviteOffer {
    /** One pending invite per kind and sender: a new one replaces the old. */
    kind: string;
    from: Player;
    /** The line on the invitations card and in the menu. */
    summary: string;
    /** The toast; "/accept or /decline." follows it. */
    text: string;
    accept: InviteAnswer;
    decline?: InviteAnswer;
}

/** A pending invite as the city menu lists it. */
export interface InviteInfo {
    id: number;
    kind: string;
    from: string;
    summary: string;
}

interface Invite {
    id: number;
    kind: string;
    fromId: number;
    fromName: string;
    summary: string;
    accept: InviteAnswer;
    decline?: InviteAnswer;
    expiresAt: number;
}

// By the target's id.
const pending = new PlayerMap<Invite[]>();
let nextId = 1;

function live(player: Player): Invite[] {
    const now = Date.now();
    const list = (pending.get(player.id) ?? []).filter(
        (invite) => invite.expiresAt > now && playerById(invite.fromId),
    );

    pending.set(player.id, list);

    return list;
}

function showCard(player: Player): void {
    const list = live(player);

    card(
        player,
        'invites',
        list.length
            ? {
                  title: list.length === 1 ? 'Invitation' : `${list.length} invitations`,
                  subtitle: 'Type /accept or /decline, or use the F2 menu',
                  rows: list.map((invite) => [invite.fromName, invite.summary]),
                  tone: 'gold',
              }
            : null,
    );
}

/** Puts the offer to the target; returns its id. */
export function offer(target: Player, invite: InviteOffer): number {
    const list = live(target).filter(
        (entry) => !(entry.kind === invite.kind && entry.fromId === invite.from.id),
    );
    const entry: Invite = {
        id: nextId++,
        kind: invite.kind,
        fromId: invite.from.id,
        fromName: invite.from.nickname,
        summary: invite.summary,
        accept: invite.accept,
        decline: invite.decline,
        expiresAt: Date.now() + INVITE_MS,
    };

    list.push(entry);
    pending.set(target.id, list);
    toast(target, `${invite.text} /accept or /decline.`, {
        title: invite.from.nickname,
        tone: 'gold',
        seconds: 10,
    });

    showCard(target);
    setTimeout(() => showCard(target), INVITE_MS + 100);

    return entry.id;
}

// The invite the answer names (a sender's name prefix or an id), else the latest.
function take(player: Player, args: readonly string[]): Invite | null {
    const list = live(player);
    const query = args.join(' ').trim().toLowerCase();
    const index = query
        ? list.findIndex(
              (invite) =>
                  invite.fromName.toLowerCase().startsWith(query) || String(invite.id) === query,
          )
        : list.length - 1;

    if (index < 0) {
        return null;
    }

    const [invite] = list.splice(index, 1);

    showCard(player);

    return invite;
}

/** Takes back every invite of this kind the player sent. */
export function withdraw(from: Player, kind: string): void {
    for (const [targetId, list] of pending) {
        const kept = list.filter((invite) => !(invite.fromId === from.id && invite.kind === kind));

        if (kept.length !== list.length) {
            pending.set(targetId, kept);
            const target = playerById(targetId);

            if (target) {
                showCard(target);
            }
        }
    }
}

/** The player's pending invites, latest last. */
export function listFor(player: Player): InviteInfo[] {
    return live(player).map(({ id, kind, fromName, summary }) => ({
        id,
        kind,
        from: fromName,
        summary,
    }));
}

commands.register(
    {
        name: 'accept',
        aliases: ['yes'],
        usage: '/accept [player]',
        help: 'Accept the latest invitation.',
        category: 'Social',
    },
    (player, args) => {
        const invite = take(player, args);

        if (!invite) {
            toast(player, 'No invitation is waiting.');

            return;
        }

        const from = playerById(invite.fromId);

        if (from) {
            invite.accept(player, from);
        }
    },
);

commands.register(
    {
        name: 'decline',
        aliases: ['no'],
        usage: '/decline [player]',
        help: 'Turn down the latest invitation.',
        category: 'Social',
    },
    (player, args) => {
        const invite = take(player, args);

        if (!invite) {
            return;
        }

        const from = playerById(invite.fromId);

        if (from) {
            toast(from, `${player.nickname} declined.`);
            invite.decline?.(player, from);
        }
    },
);

// A player who leaves takes back what they sent; their own list goes with them.
events.on('playerDisconnect', (player) => {
    for (const [targetId, list] of pending) {
        pending.set(
            targetId,
            list.filter((invite) => invite.fromId !== player.id),
        );
    }
});
