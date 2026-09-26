import type { Point } from '../shared/protocol';

// Stock 9dumch4 includes its stepped roof; 9dumch1 is an open-roof shell.
export const HOUSE_PICKUP_MODEL = '9dumch4.i3d';
export const HOUSE_PICKUP_SCALE = 0.12;

export function housePose(entrance: Point, time: number) {
    const yaw = time / 900;
    const cosine = Math.cos(yaw),
        sine = Math.sin(yaw);
    // Center the stock model's geometry on the rotation axis. Bounds measured
    // from its local mesh: x [-4.201,4.307], z [-1.875,2.125].
    const cx = 0.053424 * HOUSE_PICKUP_SCALE;
    const cz = 0.124893 * HOUSE_PICKUP_SCALE;

    return {
        position: {
            x: entrance.x - cosine * cx - sine * cz,
            y: entrance.y + 0.65,
            z: entrance.z + sine * cx - cosine * cz,
        },
        rotation: { w: Math.cos(yaw / 2), x: 0, y: -Math.sin(yaw / 2), z: 0 },
    };
}

export function arrowRotation(entrance: Point, viewer: Point) {
    const yaw = Math.atan2(viewer.x - entrance.x, viewer.z - entrance.z) / 2;
    const c = Math.cos(yaw) * Math.SQRT1_2,
        s = Math.sin(yaw) * Math.SQRT1_2;

    // sipka points and animates along +X. LS3D applies row-vector rotations,
    // so conjugate the yaw/roll quaternion to point down at every viewing angle.
    return { w: c, x: s, y: -s, z: c };
}
