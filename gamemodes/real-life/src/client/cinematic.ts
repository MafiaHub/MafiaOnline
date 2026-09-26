import { START_CAMERA, copyPose, type CameraPose, type CameraSpline } from '../shared/camera';
import { createCameraFlight } from './camera-flight';

const fadeMs = 700;

export class Cinematic {
    private splines: CameraSpline[] = [];
    private index = 0;
    private elapsed = 0;
    private fading = false;
    private flight: ((progress: number) => CameraPose) | null = null;
    private readonly originalFov = Camera.getFov();
    pose: CameraPose = copyPose(START_CAMERA);
    reducedMotion = false;
    editing = false;

    constructor(splines: CameraSpline[] = []) {
        this.setSplines(splines);
        this.apply(this.flight?.(0) ?? this.pose);
        Camera.setFov(58);
        Camera.setRange(0.1, 550);
        Fade.in(fadeMs / 1000);
    }

    setSplines(splines: CameraSpline[]): void {
        this.splines = splines;
        this.index = 0;
        this.startShot();
    }

    private startShot(): void {
        const spline = this.splines[this.index];

        this.flight = spline ? createCameraFlight(spline.points) : null;
        this.elapsed = 0;
        this.fading = false;
    }

    apply(pose: CameraPose): void {
        this.pose = pose;
        Camera.lock(pose.position, pose.direction, pose.roll ?? 0);
    }

    update(delta: number): void {
        if (this.editing) {
            return;
        }

        const spline = this.splines[this.index];

        if (!spline || !this.flight) {
            this.apply(this.pose);

            return;
        }

        if (!this.reducedMotion) {
            this.elapsed += delta;
        }

        const duration = spline.seconds * 1000;

        if (!this.reducedMotion && this.elapsed >= duration && !this.fading) {
            Fade.out(fadeMs / 1000);
            this.fading = true;
        }

        if (this.elapsed >= duration + fadeMs) {
            this.index = (this.index + 1) % this.splines.length;
            this.startShot();
            Fade.in(fadeMs / 1000);
        }

        this.apply(
            this.flight!(Math.min(this.elapsed / (this.splines[this.index].seconds * 1000), 1)),
        );
    }

    stop(): void {
        Camera.unlock();

        if (this.originalFov !== null) {
            Camera.setFov(this.originalFov);
        }

        Fade.in(0.35);
    }
}
