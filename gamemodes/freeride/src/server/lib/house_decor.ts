// What a home owner can buy to put in their home (server/housing/): stock
// a2.dta props and furniture, the same pieces the story missions furnish
// their rooms with. `tier` is the cheapest home that sells it (the villa set
// of the story missions only goes into villas), `box` the model's bounding
// box [minX, maxX, minY, maxY, minZ, maxZ] from its LOD0 mesh, so a piece
// stands on the floor and keeps clear of the walls. Prices sit against the
// jobs: a lamp is one call-out, a good sofa a fire, the grand pieces a night
// of rum runs. A piece is paid for when it is put down; selling one back
// (Delete while carrying it) refunds it in full: decorating is meant to be
// tried and changed.
import type { HouseTier } from '../data/types';
import type { Box } from './house_rooms';

const TIER_RANK: Readonly<Record<HouseTier, number>> = { poor: 0, middle: 1, rich: 2 };

// How many pieces each kind of home holds.
export const LIMITS: Readonly<Record<HouseTier, number>> = { poor: 10, middle: 20, rich: 36 };

export const CATEGORIES = ['Furniture', 'Lamps', 'Plants', 'Pictures', 'Small things'] as const;

export type DecorCategory = (typeof CATEGORIES)[number];

export interface DecorItem {
    id: string;
    /** The model's file name without its extension. */
    model: string;
    name: string;
    price: number;
    /** The cheapest home that sells it. */
    tier: HouseTier;
    category: DecorCategory;
    box: Box;
}

type ItemRow = [string, string, string, number, HouseTier, DecorCategory, Box];

// [id, model, name, price, cheapest tier, category, box]
// prettier-ignore
const ROWS: readonly ItemRow[] = [
    ['chair', 'xzidle 01', 'Kitchen chair', 60, 'poor', 'Furniture', [-0.22, 0.3, -0.52, 0.58, -0.25, 0.25]],
    ['table', 'xstul10', 'Plank table', 110, 'poor', 'Furniture', [-0.5, 0.5, -0.8, 0.05, -0.75, 0.75]],
    ['cupboard', 'xskrin 05', 'Low cupboard', 140, 'poor', 'Furniture', [-0.63, 0.67, -0.96, 0.03, -0.3, 0.19]],
    ['cot', 'xpostel 2', 'Single bed', 240, 'poor', 'Furniture', [-0.64, 0.66, -0.41, 0.7, -0.38, 2.04]],
    ['stove', '9KAMN', 'Iron stove', 220, 'poor', 'Furniture', [-0.54, 0.25, -0.6, 1.71, -0.25, 0.24]],
    ['armchair', 'xkreslo 2', 'Leather armchair', 260, 'middle', 'Furniture', [-0.37, 0.37, -0.36, 1.02, -0.24, 0.5]],
    ['sofa', 'xgauc 01', 'Sofa', 520, 'middle', 'Furniture', [-1.2, 1.19, 0, 1.02, -1.06, -0.06]],
    ['coffee', 'xstul01', 'Coffee table', 180, 'middle', 'Furniture', [-0.74, 0.74, -0.35, 0.35, -0.51, 0.51]],
    ['dining', 'xstul05', 'Dining table', 300, 'middle', 'Furniture', [-1.25, 1.25, -0.85, 0.05, -0.55, 0.55]],
    ['wardrobe', 'xskrin 21', 'Wardrobe', 380, 'middle', 'Furniture', [-0.63, 0.63, -0.08, 2, -0.36, 0.32]],
    ['sideboard', 'xskrin 06', 'Sideboard', 240, 'middle', 'Furniture', [-0.55, 0.55, -0.05, 0.92, -0.4, 0.35]],
    ['bed', 'xpostel 1', 'Double bed', 420, 'middle', 'Furniture', [-1.46, 0.54, -0.7, 0.66, -0.4, 2.02]],
    ['clock', '9skrinhod', 'Clock on a side table', 450, 'middle', 'Furniture', [-0.34, 0.37, -0.32, 1.2, -0.2, 0.23]],
    ['piano', 'xpiano', 'Upright piano', 1400, 'middle', 'Furniture', [-0.95, 0.91, -0.04, 1.59, -0.31, 0.33]],
    ['billiards', 'xkulecnik', 'Billiard table', 2200, 'middle', 'Furniture', [-0.11, 1.31, -0.84, 0.05, -1.88, 0.68]],
    ['chesterfield', '9kresl1', 'Chesterfield armchair', 800, 'rich', 'Furniture', [-0.75, 0.75, -0.15, 1.07, -0.51, 0.66]],
    ['ottoman', '9otoman', 'Chesterfield sofa', 1100, 'rich', 'Furniture', [-1.5, 1.51, -0.15, 1.05, -0.51, 0.68]],
    ['villachair', '9zidle2', 'Carved chair', 240, 'rich', 'Furniture', [-0.39, 0.27, -0.53, 1.03, -0.28, 0.44]],
    ['cabinet', '9skrn3b', 'China cabinet', 900, 'rich', 'Furniture', [-0.68, 0.68, -1.25, 1.01, -0.35, 0.34]],
    ['bookcase', '9knih', 'Bookcase', 600, 'rich', 'Furniture', [-0.92, 0.93, -1.66, 1.62, -0.29, 0.23]],
    ['fireplace', '9krb', 'Marble fireplace', 1500, 'rich', 'Furniture', [-0.96, 0.98, -0.67, 0.97, -0.14, 0.56]],
    ['villaclock', '9hodin1', 'Long-case clock', 1200, 'rich', 'Furniture', [-0.25, 0.27, -1.05, 1.06, -0.21, 0.18]],
    ['tablelamp', '9lampic2', 'Table lamp', 120, 'poor', 'Lamps', [-0.21, 0.21, 0, 0.73, -0.18, 0.18]],
    ['floorlamp', '9lampa1', 'Floor lamp', 220, 'middle', 'Lamps', [-0.2, 0.22, -0.72, 0.92, -0.24, 0.25]],
    ['chandelier', '9lustr4 on', 'Crystal chandelier', 900, 'rich', 'Lamps', [-0.48, 0.48, -0.02, 0.84, -0.48, 0.48]],
    ['flower', 'xkvet01', 'Potted flower', 60, 'poor', 'Plants', [-0.2, 0.2, 0, 0.65, -0.23, 0.23]],
    ['planter', 'xkvet02', 'Planter', 90, 'poor', 'Plants', [-0.49, 0.49, -0.41, 0.41, -0.49, 0.49]],
    ['ficus', 'fikus', 'Ficus', 380, 'middle', 'Plants', [-0.53, 0.49, -0.16, 1.82, -0.46, 0.63]],
    ['palm', '9palma', 'Palm', 450, 'rich', 'Plants', [-0.93, 0.79, -0.16, 1.6, -0.82, 0.89]],
    ['print', 'xobraz01', 'Framed print', 80, 'poor', 'Pictures', [-0.23, 0.23, -0.23, 0.23, -0.02, 0]],
    ['landscape', 'xobraz09', 'Landscape', 120, 'poor', 'Pictures', [-0.23, 0.23, 0, 0.45, -0.01, 0.01]],
    ['painting', 'xobraz05', 'Framed painting', 200, 'middle', 'Pictures', [-0.23, 0.23, -0.22, 0.22, -0.02, 0]],
    ['portrait', '9obr8', 'Oil painting', 800, 'rich', 'Pictures', [-0.51, 0.52, -0.54, 0.54, -0.03, 0.02]],
    ['mirror', '9zrcadlo', 'Mirror', 260, 'middle', 'Pictures', [-0.28, 0.28, -0.28, 0.28, 0.02, 0.05]],
    ['trophy', '9srnec', 'Stag trophy', 500, 'rich', 'Pictures', [-0.8, 0.78, -0.32, 0.92, -0.34, 0.26]],
    ['bottle', 'xflaska 1', 'Bottle of rye', 25, 'poor', 'Small things', [-0.05, 0.04, 0, 0.32, -0.04, 0.04]],
    ['bottles', '9lahvic', 'Bottles', 40, 'poor', 'Small things', [-0.05, 0.05, -0.08, 0.11, -0.03, 0.04]],
    ['radio', 'xradio 1', 'Radio set', 150, 'poor', 'Small things', [-0.09, 0.09, 0, 0.45, -0.14, 0.14]],
    ['phone', 'telefon', 'Telephone', 180, 'middle', 'Small things', [-0.11, 0.26, -0.02, 0.23, -0.17, 0.15]],
    ['vase', '9vaza2', 'Porcelain vase', 150, 'middle', 'Small things', [-0.15, 0.15, -0.08, 0.38, -0.14, 0.14]],
    ['rug', 'xkoberec', 'Rug', 220, 'poor', 'Small things', [-1.4, 2.5, 0, 0, -1.4, 2.5]],
    ['carpet', '9kober1', 'Persian carpet', 700, 'rich', 'Small things', [-2.24, 2.24, -0.02, -0.02, -1.44, 1.44]],
    ['suitcase', 'xtaska 1', 'Suitcase', 70, 'poor', 'Small things', [-0.38, 0.38, 0, 0.6, -0.1, 0.1]],
];

export const ITEMS: readonly DecorItem[] = ROWS.map(
    ([id, model, name, price, tier, category, box]) =>
        Object.freeze({ id, model, name, price, tier, category, box }),
);

const BY_ID = new Map(ITEMS.map((item) => [item.id, item]));

// Rugs, lamps and pictures do not collide.
export const PASSABLE: ReadonlySet<string> = new Set([
    'rug',
    'carpet',
    'chandelier',
    'print',
    'landscape',
    'painting',
    'portrait',
    'mirror',
    'trophy',
]);
// Pieces that hang on a wall: they may lie flat against it.
export const WALL: ReadonlySet<string> = new Set([
    'print',
    'landscape',
    'painting',
    'portrait',
    'mirror',
    'trophy',
]);

export function byId(id: unknown): DecorItem | null {
    return BY_ID.get(String(id)) ?? null;
}

export function sells(tier: HouseTier, item: DecorItem): boolean {
    return TIER_RANK[tier] >= TIER_RANK[item.tier];
}

export function forTier(tier: HouseTier): DecorItem[] {
    return ITEMS.filter((item) => sells(tier, item));
}
