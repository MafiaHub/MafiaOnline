// Reading what a client sends (onClient): plain JSON nobody vouches for, so
// every field is checked before it is used.

/** The payload's fields; none when it is not an object. */
export function fields(payload: unknown): Record<string, unknown> {
    return typeof payload === 'object' && payload !== null
        ? (payload as Record<string, unknown>)
        : {};
}

export function isFiniteNumber(value: unknown): value is number {
    return typeof value === 'number' && Number.isFinite(value);
}
