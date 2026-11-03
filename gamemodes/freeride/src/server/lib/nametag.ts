// Nametags show the crew tag and colour, blue for police on duty, and turn
// red while a bounty is out.
import { PlayerMap } from './players';

/** The crew a player belongs to, as their public `fr.crew` value carries it. */
export interface CrewTag {
    tag: string;
    name: string;
    color: string;
}

const defaults = new PlayerMap<number>();

function hex(color: string): number {
    return parseInt(color.slice(1), 16);
}

function colorOf(player: Player): number {
    // getNametagColor reads 0xAARRGGBB; setNametag takes 0xRRGGBB.
    let color = defaults.get(player.id);

    if (color === undefined) {
        color = player.getNametagColor() & 0xffffff;
        defaults.set(player.id, color);
    }

    return color;
}

export function refresh(player: Player): void {
    const original = colorOf(player);
    const crew: CrewTag | null | undefined = player.state.get('fr.crew');
    const bounty: unknown = player.state.get('fr.bounty');
    const duty: unknown = player.state.get('fr.duty');

    player.setNametag({
        color: bounty ? 0xd0574a : duty ? 0x6fa8ff : crew ? hex(crew.color) : original,
        text: duty ? `[POLICE] ${player.nickname}` : crew ? `[${crew.tag}] ${player.nickname}` : '',
    });
}

// Back to the colour and name the player had before this resource tinted them.
export function reset(player: Player): void {
    const color = defaults.get(player.id);

    if (color !== undefined) {
        player.setNametag({ color, text: '' });
    }

    defaults.delete(player.id);
}
