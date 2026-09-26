import type { CameraPose } from '../shared/camera';
import type { Point } from '../shared/protocol';
import { catmullRom } from './spline';

/** Open Catmull–Rom path: start/end at the recorded points, with no forced loop. */
export function createCameraFlight(
    points: readonly CameraPose[],
): (progress: number) => CameraPose {
    if (points.length < 2) {
        throw new Error('A camera spline needs two points.');
    }

    const angles: Point[] = [];

    const unwrap = (angle: number, previous = angle) =>
        previous + Math.atan2(Math.sin(angle - previous), Math.cos(angle - previous));

    for (const { direction, roll = 0 } of points) {
        const previous = angles.at(-1);

        // Cross angle boundaries using the shortest turn for both heading and roll.
        angles.push({
            x: unwrap(Math.atan2(direction.x, direction.z), previous?.x),
            y: Math.atan2(direction.y, Math.hypot(direction.x, direction.z)),
            z: unwrap(roll, previous?.z),
        });
    }

    const sample = (progress: number): CameraPose => {
        const scaled = Math.max(0, Math.min(progress, 1)) * (points.length - 1);
        const index = Math.min(Math.floor(scaled), points.length - 2);
        const at = (offset: number) => Math.max(0, Math.min(index + offset, points.length - 1));
        const indices = [at(-1), at(0), at(1), at(2)];
        const [a, b, c, d] = indices;
        const t = scaled - index;
        const angle = catmullRom(angles[a], angles[b], angles[c], angles[d], t);

        return {
            position: catmullRom(
                points[a].position,
                points[b].position,
                points[c].position,
                points[d].position,
                t,
            ),
            direction: {
                x: Math.sin(angle.x) * Math.cos(angle.y),
                y: Math.sin(angle.y),
                z: Math.cos(angle.x) * Math.cos(angle.y),
            },
            roll: angle.z,
        };
    };

    const count = Math.max(256, points.length * 32);
    const lengths = [0];
    let previous = sample(0).position;

    for (let i = 1; i <= count; i++) {
        const current = sample(i / count).position;

        lengths.push(
            lengths[i - 1] +
                Math.hypot(current.x - previous.x, current.y - previous.y, current.z - previous.z),
        );

        previous = current;
    }

    return (progress) => {
        const clamped = Math.max(0, Math.min(progress, 1));
        const total = lengths[count];

        if (total < 0.001) {
            return sample(clamped);
        }

        const distance = clamped * total;
        let lo = 0;
        let hi = count;

        while (hi - lo > 1) {
            const mid = (lo + hi) >> 1;

            if (lengths[mid] < distance) {
                lo = mid;
            } else {
                hi = mid;
            }
        }

        const span = lengths[hi] - lengths[lo];

        return sample((lo + (span ? (distance - lengths[lo]) / span : 0)) / count);
    };
}
