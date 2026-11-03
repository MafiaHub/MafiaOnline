// A tobacco plant's stages: seedling, young, leafy, then ripe. Each needs
// water to grow; a plant short of water or eaten by hornworms droops.
import type { Plant } from './state';

// Growing time of each stage with water: seedling, young, leafy; then ripe.
export const STAGE_MS = [70_000, 90_000, 90_000];
export const RIPE = STAGE_MS.length;
// A dry plant keeps its quality this long, then loses it.
export const DRY_GRACE_MS = 60_000;
// How each stage looks (scale of the bush).
const LOOKS = [
    { x: 0.22, y: 0.22 },
    { x: 0.45, y: 0.5 },
    { x: 0.7, y: 0.85 },
    { x: 0.85, y: 1.25 },
];
const DROOP = 0.7;

export function ripe(plant: Plant): boolean {
    return plant.stage >= RIPE;
}

/** Losing quality: hornworms, or dry for too long. */
export function wilting(plant: Plant): boolean {
    return plant.pest || (plant.dry && plant.dryMs > DRY_GRACE_MS);
}

/** The scale of the plant's bush. */
export function lookOf(plant: Plant): { x: number; y: number; z: number } {
    const look = LOOKS[Math.min(plant.stage, RIPE)];

    return { x: look.x, y: wilting(plant) ? look.y * DROOP : look.y, z: look.x };
}
