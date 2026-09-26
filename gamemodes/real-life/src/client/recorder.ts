import {
    CAMERA_LIMITS,
    copyPose,
    type CameraPose,
    type CameraPaths,
    type CameraSpline,
    type RecorderState,
    type RecorderStatus,
} from '../shared/camera';
import { isRecord } from '../shared/protocol';
import { createCameraFlight } from './camera-flight';

export class CameraRecorder {
    private splines: CameraSpline[] = [];
    private current: CameraSpline = { seconds: 20, points: [] };
    private keys = new Set<string>();
    private yaw: number;
    private pitch: number;
    private roll: number;
    private elapsed = 0;
    private lastPublished = 0;
    private preview: ReturnType<typeof createCameraFlight> | null = null;
    private dirty = false;
    private status: RecorderStatus = 'ready';
    active = false;
    enabled = false;

    constructor(
        private readonly camera: {
            pose: CameraPose;
            editing: boolean;
            apply(pose: CameraPose): void;
        },
        private readonly publish: (state: RecorderState) => void,
        private readonly save: (paths: CameraPaths) => void,
        private readonly mission: string,
        private readonly fadeIn: () => void,
    ) {
        const { direction } = camera.pose;

        this.yaw = Math.atan2(direction.x, direction.z);
        this.pitch = Math.atan2(direction.y, Math.hypot(direction.x, direction.z));
        this.roll = camera.pose.roll ?? 0;
    }

    load(paths: CameraPaths): void {
        if (this.dirty || this.active) {
            return;
        }

        this.splines = paths.splines.map((spline) => ({
            seconds: spline.seconds,
            points: spline.points.map(copyPose),
        }));

        this.current = { seconds: 20, points: [] };
    }

    private stopPreview(): void {
        this.preview = null;
        this.elapsed = 0;
        const { direction } = this.camera.pose;

        this.yaw = Math.atan2(direction.x, direction.z);
        this.pitch = Math.atan2(direction.y, Math.hypot(direction.x, direction.z));
        this.roll = this.camera.pose.roll ?? 0;
    }

    command(value: unknown): void {
        if (!this.enabled || !isRecord(value)) {
            return;
        }

        if (value.action === 'toggle') {
            this.active = !this.active;
            this.camera.editing = this.active;
            this.keys.clear();
            this.stopPreview();
            this.fadeIn();
            this.report();

            return;
        }

        if (!this.active) {
            return;
        }

        switch (value.action) {
            case 'keys':
                this.keys = new Set(
                    Array.isArray(value.keys)
                        ? value.keys
                              .filter((key): key is string => typeof key === 'string')
                              .slice(0, 12)
                        : [],
                );

                return;
            case 'look':
                if (
                    !this.preview &&
                    typeof value.x === 'number' &&
                    typeof value.y === 'number' &&
                    Number.isFinite(value.x) &&
                    Number.isFinite(value.y)
                ) {
                    this.yaw += Math.max(-200, Math.min(value.x, 200)) * 0.004;
                    this.pitch = Math.max(
                        -1.45,
                        Math.min(this.pitch - Math.max(-200, Math.min(value.y, 200)) * 0.004, 1.45),
                    );
                }

                return;
            case 'roll':
                if (!this.preview && typeof value.x === 'number' && Number.isFinite(value.x)) {
                    const angle = this.roll + Math.max(-200, Math.min(value.x, 200)) * 0.004;

                    this.roll = Math.atan2(Math.sin(angle), Math.cos(angle));
                }

                return;
            case 'point':
                this.stopPreview();

                if (
                    this.current.points.length >= CAMERA_LIMITS.points ||
                    this.splines.length >= CAMERA_LIMITS.splines
                ) {
                    this.status = 'limit';
                    break;
                }

                this.current.points.push(copyPose(this.camera.pose));
                this.dirty = true;
                this.status = 'pointAdded';
                break;
            case 'new':
                this.stopPreview();

                if (this.current.points.length < 2) {
                    this.status = 'needPoints';
                    break;
                }

                this.splines.push(this.current);
                this.current = { seconds: 20, points: [] };
                this.dirty = true;
                this.status = 'newSpline';
                break;
            case 'undo':
                this.stopPreview();

                if (this.current.points.length) {
                    this.current.points.pop();
                    this.dirty = true;
                }

                break;
            case 'duration':
                if (typeof value.seconds === 'number' && Number.isFinite(value.seconds)) {
                    this.current.seconds = Math.max(3, Math.min(value.seconds, 180));
                    this.dirty = true;
                }

                break;
            case 'play':
                if (this.preview) {
                    this.stopPreview();
                } else if (this.current.points.length >= 2) {
                    this.preview = createCameraFlight(this.current.points);
                    this.elapsed = 0;
                    this.keys.clear();
                } else {
                    this.status = 'needPoints';
                }

                break;
            case 'save': {
                const splines = [
                    ...this.splines,
                    ...(this.current.points.length >= 2 ? [this.current] : []),
                ];

                if (!splines.length || this.current.points.length === 1) {
                    this.status = 'needPoints';
                    break;
                }

                this.save({ version: 1, mission: this.mission, splines });
                break;
            }
        }

        this.report();
    }

    saved(ok: boolean): void {
        this.status = ok ? 'saved' : 'saveFailed';
        this.dirty = ok ? false : this.dirty;
        this.report();
    }

    update(delta: number): void {
        if (!this.active) {
            return;
        }

        const seconds = delta / 1000;

        if (this.preview) {
            this.elapsed += seconds;
            this.camera.apply(this.preview(Math.min(this.elapsed / this.current.seconds, 1)));

            if (this.elapsed >= this.current.seconds) {
                this.stopPreview();
                this.report();
            }
        } else {
            const axis = (positive: string, negative: string) =>
                Number(this.keys.has(positive)) - Number(this.keys.has(negative));

            this.yaw += axis('ArrowRight', 'ArrowLeft') * seconds * 0.8;
            this.pitch = Math.max(
                -1.45,
                Math.min(this.pitch + axis('ArrowUp', 'ArrowDown') * seconds * 0.8, 1.45),
            );

            const direction = {
                x: Math.sin(this.yaw) * Math.cos(this.pitch),
                y: Math.sin(this.pitch),
                z: Math.cos(this.yaw) * Math.cos(this.pitch),
            };
            const forward = axis('KeyW', 'KeyS');
            const right = axis('KeyD', 'KeyA');
            const up = axis('KeyE', 'KeyQ');
            const multiplier =
                this.keys.has('ShiftLeft') || this.keys.has('ShiftRight')
                    ? 4
                    : this.keys.has('AltLeft') || this.keys.has('AltRight')
                      ? 0.25
                      : 1;
            const distance =
                (8 * seconds * multiplier) / Math.max(1, Math.hypot(forward, right, up));
            const position = this.camera.pose.position;

            this.camera.apply({
                position: {
                    x: position.x + (direction.x * forward + Math.cos(this.yaw) * right) * distance,
                    y: position.y + (direction.y * forward + up) * distance,
                    z: position.z + (direction.z * forward - Math.sin(this.yaw) * right) * distance,
                },
                direction,
                roll: this.roll,
            });
        }

        this.lastPublished += delta;

        if (this.lastPublished >= 200) {
            this.lastPublished = 0;
            this.report();
        }
    }

    report(): void {
        this.publish({
            active: this.active,
            replaying: this.preview !== null,
            spline: this.splines.length + 1,
            points: this.current.points.length,
            total: this.splines.length,
            seconds: this.current.seconds,
            dirty: this.dirty,
            status: this.status,
            position: this.camera.pose.position,
            roll: this.camera.pose.roll ?? 0,
        });
    }
}
