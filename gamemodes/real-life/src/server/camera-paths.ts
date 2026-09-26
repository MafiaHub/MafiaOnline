import { existsSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { isCameraPaths, type CameraPaths } from '../shared/camera';
import loginCamera from '../shared/login-camera.json';

export function loadCameraPaths(filename: string, mission: string): CameraPaths {
    const empty: CameraPaths = { version: 1, mission, splines: [] };

    const value: unknown = existsSync(filename)
        ? JSON.parse(readFileSync(filename, 'utf8'))
        : loginCamera;

    if (!isCameraPaths(value)) {
        throw new Error('Invalid camera paths file.');
    }

    if (value.mission === mission) {
        return value;
    }

    return loginCamera.mission === mission ? { ...loginCamera, version: 1 } : empty;
}

export function saveCameraPaths(filename: string, paths: CameraPaths): void {
    if (!isCameraPaths(paths)) {
        throw new Error('Invalid camera paths.');
    }

    writeFileSync(`${filename}.tmp`, JSON.stringify(paths, null, 2) + '\n');
    renameSync(`${filename}.tmp`, filename);
}
