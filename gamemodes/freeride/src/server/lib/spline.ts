// Closed centripetal Catmull-Rom curves (alpha 0.5, Barry and Goldman's
// pyramid), resampled at constant arc length, with a bank angle from the
// curvature of the ground track. lib/flyover.ts builds the login camera's
// paths with it; the client only interpolates the resulting table.
import type { Point } from './util';

export const ALPHA = 0.5;
const EPSILON = 1e-4;
const GRAVITY = 9.81;

export interface PathSample {
    /** The curve parameter: control point i is at s = i. */
    s: number;
    position: Point;
    /** Where the camera looks from there. */
    target: Point;
}

export interface Path {
    length: number;
    /** The arc length between samples. */
    step: number;
    samples: PathSample[];
}

export interface BankOptions {
    /** The speed the turns are taken at, m/s. */
    speed: number;
    gain: number;
    maxRoll: number;
    smoothMetres: number;
}

function lerp(a: Point, b: Point, t: number): Point {
    return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t };
}

function span(a: Point, b: Point): number {
    return Math.max(EPSILON, Math.hypot(b.x - a.x, b.y - a.y, b.z - a.z) ** ALPHA);
}

/** The point at u (0..1) between p1 and p2. */
export function centripetal(p0: Point, p1: Point, p2: Point, p3: Point, u: number): Point {
    const t1 = span(p0, p1);
    const t2 = t1 + span(p1, p2);
    const t3 = t2 + span(p2, p3);
    const t = t1 + (t2 - t1) * u;
    const a1 = lerp(p0, p1, t / t1);
    const a2 = lerp(p1, p2, (t - t1) / (t2 - t1));
    const a3 = lerp(p2, p3, (t - t2) / (t3 - t2));
    const b1 = lerp(a1, a2, t / t2);
    const b2 = lerp(a2, a3, (t - t1) / (t3 - t1));

    return lerp(b1, b2, (t - t1) / (t2 - t1));
}

/** A closed loop through every point; parameter s runs 0..points.length. */
export function loopAt(points: readonly Point[], s: number): Point {
    const n = points.length;
    const wrapped = ((s % n) + n) % n;
    const i = Math.floor(wrapped);
    const at = (k: number): Point => points[((k % n) + n) % n];

    return centripetal(at(i - 1), at(i), at(i + 1), at(i + 2), wrapped - i);
}

export function wrapAngle(angle: number): number {
    return Math.atan2(Math.sin(angle), Math.cos(angle));
}

/**
 * Resamples a closed loop every `step` metres of arc length. targets is a
 * second loop of the same length (where the camera looks), sampled at the
 * same parameter.
 */
export function resample(
    points: readonly Point[],
    targets: readonly Point[],
    step: number,
    perSegment = 96,
): Path {
    const n = points.length;
    const fine: { s: number; distance: number }[] = [];
    let length = 0;
    let previous = loopAt(points, 0);

    for (let k = 0; k <= n * perSegment; k++) {
        const s = k / perSegment;
        const position = loopAt(points, s);

        length += Math.hypot(
            position.x - previous.x,
            position.y - previous.y,
            position.z - previous.z,
        );

        fine.push({ s, distance: length });
        previous = position;
    }

    const count = Math.max(8, Math.round(length / step));
    const samples: PathSample[] = [];
    let j = 0;

    for (let k = 0; k < count; k++) {
        const distance = (k * length) / count;

        while (j < fine.length - 2 && fine[j + 1].distance < distance) {
            j++;
        }

        const a = fine[j];
        const b = fine[j + 1];
        const f = b.distance > a.distance ? (distance - a.distance) / (b.distance - a.distance) : 0;
        const s = a.s + (b.s - a.s) * f;

        samples.push({ s, position: loopAt(points, s), target: loopAt(targets, s) });
    }

    return { length, step: length / count, samples };
}

/**
 * Bank into turns: the angle a craft at `speed` would lean for the turn
 * rate of the ground track, scaled by gain, clamped, then smoothed with a
 * circular Gaussian so the loop has no seam. Positive roll banks left
 * (S_quat::SetDir rotates the right axis towards up), and a right turn
 * raises the heading atan2(x, z), so a right turn rolls negative.
 */
export function bank(path: Path, { speed, gain, maxRoll, smoothMetres }: BankOptions): number[] {
    const { samples, step } = path;
    const n = samples.length;
    const heading = samples.map((_, i) => {
        const a = samples[(i - 1 + n) % n].position;
        const b = samples[(i + 1) % n].position;

        return Math.atan2(b.x - a.x, b.z - a.z);
    });
    const raw = heading.map((_, i) => {
        const turn = wrapAngle(heading[(i + 1) % n] - heading[(i - 1 + n) % n]) / (2 * step);
        const lean = -Math.atan((speed * speed * turn) / GRAVITY) * gain;

        return Math.max(-maxRoll, Math.min(maxRoll, lean));
    });
    const sigma = Math.max(1, smoothMetres / step);
    const reach = Math.ceil(sigma * 3);
    const weights = Array.from({ length: 2 * reach + 1 }, (_, k) =>
        Math.exp(-((k - reach) ** 2) / (2 * sigma * sigma)),
    );
    const total = weights.reduce((sum, weight) => sum + weight, 0);

    return raw.map(
        (_, i) =>
            weights.reduce((sum, weight, k) => sum + weight * raw[(i + k - reach + n * 4) % n], 0) /
            total,
    );
}
