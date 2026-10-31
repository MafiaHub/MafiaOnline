// Timers that end together: a sequence that a new start or a teardown cuts
// short (a countdown, a camera shot) or a refresh that stops with its panel.

export class Timers {
    private readonly timeouts: TimerHandle[] = [];
    private readonly intervals: TimerHandle[] = [];

    after(ms: number, run: () => void): void {
        this.timeouts.push(setTimeout(run, ms));
    }

    every(ms: number, run: () => void): void {
        this.intervals.push(setInterval(run, ms));
    }

    clear(): void {
        for (const handle of this.timeouts) {
            clearTimeout(handle);
        }

        for (const handle of this.intervals) {
            clearInterval(handle);
        }

        this.timeouts.length = 0;
        this.intervals.length = 0;
    }
}
