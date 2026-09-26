export class RateLimit {
    private readonly windows = new Map<string, { count: number; until: number }>();

    constructor(
        private readonly maximum = 6,
        private readonly windowMs = 60_000,
    ) {}

    take(key: string, now = Date.now()): boolean {
        const current = this.windows.get(key);

        if (!current || now >= current.until) {
            this.windows.set(key, { count: 1, until: now + this.windowMs });

            return true;
        }

        if (current.count >= this.maximum) {
            return false;
        }

        current.count++;

        return true;
    }

    prune(now = Date.now()): void {
        for (const [key, value] of this.windows) {
            if (now >= value.until) {
                this.windows.delete(key);
            }
        }
    }
}
