// Positions and the small sums the HUD does with them (Mafia is y-up).

/** A world position, as the API takes and returns them. */
export interface Point {
    x: number;
    y: number;
    z: number;
}

export function distance(a: Point, b: Point): number {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** The distance on the ground, ignoring height. */
export function flatDistance(a: Point, b: Point): number {
    return Math.hypot(a.x - b.x, a.z - b.z);
}

/** A plain copy of a point (a native Vector3 is a handle), safe to keep and send. */
export function plain(point: Point): Point {
    return { x: point.x, y: point.y, z: point.z };
}

/** The (unnormalised) direction from one point to another, as Camera.lock takes it. */
export function toward(from: Point, to: Point): Point {
    return { x: to.x - from.x, y: to.y - from.y, z: to.z - from.z };
}

export function clamp(value: number, min: number, max: number): number {
    return Math.max(min, Math.min(max, value));
}
