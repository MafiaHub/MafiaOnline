// One-shot timers that end with the resource: a round that opens later, a
// map switch after its warning, a save gathered over a second.
import { onStop } from './lifecycle';

export interface DelayOptions {
    /**
     * The resource's stop runs a call still waiting, at once, instead of
     * dropping it: a save, which a restart would otherwise lose.
     */
    runOnStop?: boolean;
}

/** At most one delayed call at a time; the resource's stop cancels it (or runs it). */
export class Delay {
    private timer: ReturnType<typeof setTimeout> | null = null;
    private call: (() => void) | null = null;

    constructor({ runOnStop = false }: DelayOptions = {}) {
        onStop(() => (runOnStop ? this.flush() : this.cancel()));
    }

    /** Whether a call is waiting. */
    get pending(): boolean {
        return this.timer !== null;
    }

    /** Runs `call` after `ms`, in place of any call still waiting. */
    start(ms: number, call: () => void): void {
        this.cancel();
        this.call = call;
        this.timer = setTimeout(() => this.flush(), ms);
    }

    /** Runs the waiting call now, if there is one. */
    flush(): void {
        const call = this.call;

        this.cancel();
        call?.();
    }

    cancel(): void {
        if (this.timer !== null) {
            clearTimeout(this.timer);
        }

        this.timer = null;
        this.call = null;
    }
}
