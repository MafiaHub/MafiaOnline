import { isPoint, isRecord, type Point } from './protocol';

export interface CameraPose {
    position: Point;
    direction: Point;
    /** Radians around the viewing direction; older recordings default to zero. */
    roll?: number;
}

export interface CameraSpline {
    seconds: number;
    points: CameraPose[];
}

export interface CameraPaths {
    version: 1;
    mission: string;
    splines: CameraSpline[];
}

export type RecorderStatus =
    'ready' | 'pointAdded' | 'newSpline' | 'needPoints' | 'saved' | 'saveFailed' | 'limit';

export interface RecorderState {
    active: boolean;
    replaying: boolean;
    spline: number;
    points: number;
    total: number;
    seconds: number;
    dirty: boolean;
    status: RecorderStatus;
    position: Point;
    roll: number;
}

export const CAMERA_LIMITS = { splines: 32, points: 128 } as const;

// Fallback for missions without recorded camera paths.
export const START_CAMERA: CameraPose = {
    position: { x: -1774.5, y: -1.3, z: 5.2 },
    direction: { x: 0.7, y: 0.02, z: 0.7 },
};

export function isCameraPaths(value: unknown): value is CameraPaths {
    return (
        isRecord(value) &&
        value.version === 1 &&
        typeof value.mission === 'string' &&
        /^[a-z0-9_-]{1,64}$/i.test(value.mission) &&
        Array.isArray(value.splines) &&
        value.splines.length <= CAMERA_LIMITS.splines &&
        value.splines.every(
            (spline) =>
                isRecord(spline) &&
                typeof spline.seconds === 'number' &&
                Number.isFinite(spline.seconds) &&
                spline.seconds >= 3 &&
                spline.seconds <= 180 &&
                Array.isArray(spline.points) &&
                spline.points.length >= 2 &&
                spline.points.length <= CAMERA_LIMITS.points &&
                spline.points.every(
                    (pose) =>
                        isRecord(pose) &&
                        isPoint(pose.position) &&
                        isPoint(pose.direction) &&
                        (pose.roll === undefined ||
                            (typeof pose.roll === 'number' && Number.isFinite(pose.roll))) &&
                        Math.hypot(pose.direction.x, pose.direction.y, pose.direction.z) > 0.001,
                ),
        )
    );
}

export function copyPose(pose: CameraPose): CameraPose {
    return {
        position: { ...pose.position },
        direction: { ...pose.direction },
        roll: pose.roll ?? 0,
    };
}
