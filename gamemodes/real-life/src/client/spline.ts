import type { Point } from '../shared/protocol';

export function catmullRom(p0: Point, p1: Point, p2: Point, p3: Point, t: number): Point {
    const coordinate = (a: number, b: number, c: number, d: number) =>
        0.5 *
        (2 * b +
            (-a + c) * t +
            (2 * a - 5 * b + 4 * c - d) * t * t +
            (-a + 3 * b - 3 * c + d) * t * t * t);

    return {
        x: coordinate(p0.x, p1.x, p2.x, p3.x),
        y: coordinate(p0.y, p1.y, p2.y, p3.y),
        z: coordinate(p0.z, p1.z, p2.z, p3.z),
    };
}
