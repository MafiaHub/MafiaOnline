// Moving a player into another virtual world behind a fade: into a home, the
// county jail, a building, and back out to the street. The screen fades out
// (client/main.ts), the player is moved, and it fades back in. The move is
// skipped if the life ended meanwhile; a move refused (a client still loading
// the mission) leaves the player where and in the world they were.
import { teleport, type Loadout } from './life';
import { PlayerSet } from './players';
import { connected, emit, type Placement } from './util';

const FADE_MS = 400;
const ARRIVE_FADE_SECONDS = 0.6;

export interface TransferOptions {
    /** What the player carries afterwards; by default what they carry when the move happens. */
    loadout?: Loadout;
    /** After the fade back in, with whether the move happened; not called when the life ended meanwhile. */
    then?: (moved: boolean) => void;
}

/** One module's moves; the player counts as moving from the fade out until the move. */
export class Transfers {
    private readonly moving = new PlayerSet();

    /** `refusedFadeSeconds`: how long the screen takes to come back after a refused move. */
    constructor(private readonly refusedFadeSeconds = 0.3) {}

    isMoving(player: Player): boolean {
        return this.moving.has(player.id);
    }

    /** No longer counts the player as moving; a move under way still happens if their life goes on. */
    forget(player: Player): void {
        this.moving.delete(player.id);
    }

    clear(): void {
        this.moving.clear();
    }

    move(player: Player, pose: Placement, world: number, options: TransferOptions = {}): void {
        const { spawnGeneration } = player;

        this.moving.add(player.id);
        emit(player, 'fr:fade', { out: true, seconds: FADE_MS / 1000 });
        setTimeout(() => {
            this.moving.delete(player.id);

            if (!connected(player) || !player.alive || player.spawnGeneration !== spawnGeneration) {
                return;
            }

            const previous = player.virtualWorld;

            player.setVirtualWorld(world);
            const moved = teleport(player, pose.position, pose.heading, options.loadout);

            if (!moved) {
                player.setVirtualWorld(previous);
            }

            emit(player, 'fr:fade', {
                out: false,
                seconds: moved ? ARRIVE_FADE_SECONDS : this.refusedFadeSeconds,
            });

            options.then?.(moved);
        }, FADE_MS);
    }
}
