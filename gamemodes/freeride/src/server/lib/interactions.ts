// What the job key (lib/jobkey.ts) does for one module where a player stands:
// the module names the action there, the hint for it on its prompt layer,
// what a press does and which actions are holds (G kept down while a bar
// fills; letting go or stepping off the spot cancels it). Hints are sent only
// when they change.
import * as jobkey from './jobkey';
import { PlayerMap } from './players';
import { playerById } from './util';

export interface Hold {
    ms: number;
    /** The prompt line while it fills. */
    text: string;
}

export interface InteractionSpec<A extends { kind: string }> {
    layer: jobkey.PromptLayer;
    /** Whether the module takes presses at all (its map is loaded). */
    enabled(): boolean;
    /** What G does for the player where they stand, or null. */
    choose(player: Player): A | null;
    hint(player: Player, action: A): string | null;
    /** A press on the action; start a hold with Interactions.hold. */
    press(player: Player, action: A): void;
    holds: Partial<Record<A['kind'], Hold>>;
    /** A hold filled up. */
    finish(player: Player, action: A): void;
    /** No hint while the module shows something else on its layer (a gauge, a car loading). */
    busy?(player: Player): boolean;
}

interface Holding<A> {
    action: A;
    hold: Hold;
    startedAt: number;
}

export class Interactions<A extends { kind: string }> {
    private readonly holding = new PlayerMap<Holding<A>>({ clearOnMissionChange: true });
    // The hint each player was sent last; none when unknown.
    private readonly hints = new PlayerMap<string | null>({ clearOnMissionChange: true });

    constructor(private readonly spec: InteractionSpec<A>) {
        jobkey.register({
            press: (player) => this.press(player),
            release: (player) => this.cancel(player),
        });
    }

    /** Starts the action's hold, if it is one. */
    hold(player: Player, action: A): void {
        const hold = this.spec.holds[action.kind as A['kind']];

        if (hold) {
            this.holding.set(player.id, { action, hold, startedAt: Date.now() });
        }
    }

    isHolding(player: Player): boolean {
        return this.holding.has(player.id);
    }

    /** Ends a hold without finishing it (the player died). */
    drop(player: Player): void {
        this.holding.delete(player.id);
    }

    /** The module's prompt line; the last hint is forgotten, so the next one is sent again. */
    prompt(player: Player, text: string | null, progress: number | null = null): void {
        this.hints.delete(player.id);
        jobkey.prompt(player, text, progress, this.spec.layer);
    }

    /** Forgets the hint sent (a fresh life shows none). */
    forget(player: Player): void {
        this.hints.delete(player.id);
    }

    /** Fills the holds; a hold whose key went up or whose action is gone ends unfinished. */
    tickHolds(now: number): void {
        for (const [id, current] of [...this.holding]) {
            const player = playerById(id);

            if (
                !player ||
                !jobkey.isHeld(player) ||
                this.spec.choose(player)?.kind !== current.action.kind
            ) {
                this.holding.delete(id);

                if (player) {
                    this.prompt(player, null);
                }

                continue;
            }

            const progress = Math.min(1, (now - current.startedAt) / current.hold.ms);

            this.prompt(player, current.hold.text, progress);

            if (progress < 1) {
                continue;
            }

            this.holding.delete(id);
            this.prompt(player, null);
            this.spec.finish(player, current.action);
        }
    }

    /** Shows every player not busy the hint for where they stand. */
    showHints(): void {
        for (const player of World.getPlayers()) {
            if (this.holding.has(player.id) || this.spec.busy?.(player)) {
                continue;
            }

            const action = this.spec.enabled() ? this.spec.choose(player) : null;
            const hint = action ? this.spec.hint(player, action) : null;

            if (this.hints.has(player.id) && this.hints.get(player.id) === hint) {
                continue;
            }

            this.hints.set(player.id, hint);
            jobkey.prompt(player, hint, null, this.spec.layer);
        }
    }

    private press(player: Player): boolean {
        if (!this.spec.enabled()) {
            return false;
        }

        const action = this.spec.choose(player);

        if (!action) {
            return false;
        }

        this.spec.press(player, action);

        return true;
    }

    private cancel(player: Player): void {
        if (this.holding.delete(player.id)) {
            this.prompt(player, null);
        }
    }
}
