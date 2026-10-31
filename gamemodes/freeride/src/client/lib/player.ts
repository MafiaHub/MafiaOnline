// The local player, as most HUD features want it.
import { events } from './runtime';

/** The local player while they are in the world and alive; null otherwise. */
export function livePlayer(): Player | null {
    const player = LocalPlayer;

    return player?.spawned && player.alive ? player : null;
}

export function onLocalSpawn(handler: (player: Player) => void): void {
    events.on('playerSpawn', (player) => {
        if (player.isLocal) {
            handler(player);
        }
    });
}

export function onLocalDeath(handler: (player: Player) => void): void {
    events.on('playerDeath', (player) => {
        if (player.isLocal) {
            handler(player);
        }
    });
}
