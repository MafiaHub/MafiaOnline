import type { Point } from './protocol';

export const PROPERTY_EVENT = {
    markers: 'real-life:property-markers',
    use: 'real-life:property-use',
} as const;
export const HOUSE_WORLD = 0x10000000;
export const INTERIOR_WORLD = 0x20000000;
export const DEFAULT_HOUSE_PRICE = 4778;
export const INTERACTION_RADIUS = 2;
export interface Pose {
    position: Point;
    heading: number;
}
export interface Parking {
    position: Point;
    rotation: { w: number; x: number; y: number; z: number };
}
export interface House {
    id: number;
    mission: string;
    entries: Pose[];
    price: number;
    interior: string | null;
    garages: Parking[];
}
export interface Interior {
    id: number;
    name: string;
    mission: string;
    exit: Pose;
}
export interface Entrance {
    id: number;
    interior: string;
    mission: string;
    entry: Pose;
}
export interface PropertyMarker {
    key: string;
    position: Point;
    kind: 'sale' | 'entry' | 'exit' | 'garage';
    label: string;
}
export function distance(a: Point, b: Point): number {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

export function heading(q: { w: number; x: number; y: number; z: number }): number {
    return Math.atan2(2 * (q.w * q.y + q.x * q.z), 1 - 2 * (q.y * q.y + q.x * q.x));
}
