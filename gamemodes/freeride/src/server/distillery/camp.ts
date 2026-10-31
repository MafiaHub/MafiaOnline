// The still's camp: the stock models (MapObject) standing around POT in
// the clearing (places.ts), kept by still.ts. It is laid out as a working
// backwoods still: a plank stall roofed with shingles over the pot, the worm
// and a mash vat, firewood stacked behind it, a packing bench and stacked
// cases by CRATES, the molasses on a pallet and the casks on a rack at
// CASKS. The stall's open side faces east, towards the yard, Hank and
// REVEAL_CAMERA, which sees the pot under the high eave; the operator stands
// there, 1.2 m from POT. The east side between the stall and the shed
// "@bouda00" stays clear for whoever comes from the yard.
//
// Every model is from the stock a2.dta and none is replaced by the Freeride
// Extended package. Their boxes [x0, x1, y0, y1, z0, z1] are measured from
// the LOD 0 meshes with their frame transforms:
//  4pristr01    [-2.18, 2.18, -1.22, 2.77, -0.69, 1.04] a plank stall: a
//               counter 1.01 m high along z -0.51 and its two ends (x ±2),
//               open at z +1.0, a shingle roof from 1.88 m over the counter
//               to 2.77 m over the open side, its floor at y 0. Its walls
//               are single-sided planes, seen only from outside, so they
//               are lined inside with Plotdrev5 (below). The back posts and
//               braces reach down to -1.22 (the stall stands on slopes) and
//               go into the ground here; the floor plane faces down.
//  Plotdrev5    [-3, 3, 0, 2.52, -0.05, 0.05] a board wall of 0LKULNA, the
//               stall's own counter texture, finished on both faces
//  xv sud       [-0.31, 0.32, 0, 0.77, -0.32, 0.32] the pot, an open drum,
//               scaled 1.5 and set upside down so its closed bottom is the
//               top the pipe leaves from
//  9sutr4       [-0.09, 0.14, -0.05, 0.10, -0.17, 0.08] a lump of rubble;
//               five, scaled 1.7, make the hearth the pot stands on, 0.2 m
//               up, over the fire still.ts lights at POT
//  xsud 01      [-0.42, 0.42, 0, 1.0, -0.42, 0.42] a wooden barrel: the
//               worm tub and the water barrel
//  xv trubka4   a pipe 1.5 m long on x, 0.16 thick
//  tutsud       [-0.39, 0.39, -0.49, 0.49, -0.39, 0.39] the mash vats
//  klady        [-4.15, 4.15, 0, 1.49, -1.82, 1.82] the scene's log pile,
//               scaled 0.25 for the firewood (2.1 x 0.37 x 0.9 m)
//  xkybl 1      [-0.19, 0.19, 0, 0.40, -0.18, 0.18] a tin bucket
//  9petrolejka  [-0.19, 0.19, -0.34, 0.15, -0.19, 0.19] a hanging kerosene
//               lamp, hung from the roof's underside over the open side
//  PALETA01     [-0.6, 0.6, -0.12, 0.06, -0.6, 0.6] a pallet
//  xpytel2      [-0.5, 0.5, -0.09, 0.12, -0.35, 0.35] a sack of molasses
//  tycka01      [-0.1, 0.1, 0, 2.75, -0.1, 0.1] a square post, laid down
//               as the rails of the cask rack
//  sud01        [-0.32, 0.32, -0.5, 0.5, -0.30, 0.30] a cask (base 0.5 m
//               below the origin), laid across the rails
//  bednakl      [-0.25, 0.25, 0, 0.5, -0.24, 0.27] a stencilled shipping
//               crate: the cases of rum
//  xstul11      [-1.0, 1.0, 0, 0.70, -0.5, 0.5] a rough plank table
//  2flask       [-0.08, 0.07, 0, 0.53, -0.08, 0.08] a labelled bottle,
//               scaled 0.6
//  kosik01      [-0.23, 0.22, -0.18, 0.44, -0.21, 0.21] a wicker basket
//  xslama 1     [-0.42, 0.42, 0, 0.84, -0.42, 0.42] a straw bale, scaled 0.8
//
// Heights: the ground under the camp is the FREEKRAJINA scene.4ds ground
// ("Object211" over the clearing, "Object91xx" and "Object217" under its
// west edge), which Freeride Extended's scene.4ds carries unchanged
// ("scene_fk", the same heights to 0.1 mm). It rises east, from 14.55 at
// the sacks to 15.0 at the casks. Each piece stands on the lowest ground
// sampled under its footprint, 2 cm down (rubble 4 cm), so on a slope its
// high side sinks a few centimetres rather than its low side floating; the
// stall's posts and walls stand on 14.66, the lowest point under its walls.
// What stands on something else (sacks, crates, bottles, casks) stands on
// that piece's top.
//
// Clearances: every piece is at least 5 m from the log piles' origins
// (header of places.ts) and at least 2.7 m from their footprints, the
// nearest being "klady7" west of the sacks; the stall is 0.87 m from
// HANK_SPOT. The revenue men walk straight to CRATES from RAID_POINTS; the
// sheds and log piles already stand in those lines; nothing here does
// before they come within 6 m of CRATES (the stall's corner is 0.4 m off
// the nearest).
import { QUARTER } from '../lib/map_objects';
import type { ObjectSpec } from '../lib/placed_objects';

export type PropId =
    | 'shelter'
    | 'shelterWest'
    | 'shelterNorth'
    | 'shelterSouth'
    | `stone${1 | 2 | 3 | 4 | 5}`
    | 'pot'
    | 'worm'
    | 'pipe'
    | 'vat1'
    | 'vat2'
    | 'firewood'
    | 'water'
    | 'bucket'
    | 'lamp'
    | 'pallet'
    | `sack${1 | 2 | 3 | 4}`
    | 'rail1'
    | 'rail2'
    | `cask${1 | 2 | 3 | 4}`
    | `crate${1 | 2 | 3 | 4 | 5 | 6 | 7}`
    | 'bench'
    | `bottle${1 | 2 | 3}`
    | 'basket'
    | 'bale';

// The cask rack's rails stand on ground 7 cm apart, so the casks lie tilted
// that much across them.
const CASK_TILT = QUARTER + 0.111;

export const PROPS: readonly (ObjectSpec & { id: PropId })[] = [
    // The stall, turned so its open side faces east (local +z to +x), the
    // pot 0.25 m into it. Its lining stands 7 cm inside each wall.
    {
        id: 'shelter',
        model: '4pristr01.i3d',
        position: { x: -1744.25, y: 14.66, z: 1250.0 },
        yaw: QUARTER,
    },
    {
        id: 'shelterWest',
        model: 'Plotdrev5.i3d',
        position: { x: -1744.69, y: 14.66, z: 1250.0 },
        yaw: QUARTER,
        scale: { x: 0.667, y: 0.4, z: 1 },
    },
    {
        id: 'shelterNorth',
        model: 'Plotdrev5.i3d',
        position: { x: -1744.0, y: 14.66, z: 1251.93 },
        scale: { x: 0.25, y: 0.4, z: 1 },
    },
    {
        id: 'shelterSouth',
        model: 'Plotdrev5.i3d',
        position: { x: -1744.0, y: 14.66, z: 1248.07 },
        scale: { x: 0.25, y: 0.4, z: 1 },
    },
    // The hearth: a ring of stones 0.4 m round POT.
    {
        id: 'stone1',
        model: '9sutr4.i3d',
        position: { x: -1743.9, y: 14.83, z: 1250.47 },
        yaw: 0.3,
        scale: 1.7,
    },
    {
        id: 'stone2',
        model: '9sutr4.i3d',
        position: { x: -1743.51, y: 14.88, z: 1250.02 },
        yaw: 1.96,
        scale: 1.7,
    },
    {
        id: 'stone3',
        model: '9sutr4.i3d',
        position: { x: -1743.81, y: 14.85, z: 1249.56 },
        yaw: 2.81,
        scale: 1.7,
    },
    {
        id: 'stone4',
        model: '9sutr4.i3d',
        position: { x: -1744.38, y: 14.81, z: 1249.7 },
        yaw: -1.81,
        scale: 1.7,
    },
    {
        id: 'stone5',
        model: '9sutr4.i3d',
        position: { x: -1744.41, y: 14.8, z: 1250.24 },
        yaw: -0.95,
        scale: 1.7,
    },
    // On the hearth, 0.2 m above POT: upside down, its origin is its top.
    {
        id: 'pot',
        model: 'xv sud.i3d',
        position: { x: -1744.0, y: 16.16, z: 1250.0 },
        rotation: { x: Math.PI, y: 0, z: 0 },
        scale: 1.5,
    },
    // The worm tub 1.35 m north of the pot; the pipe runs down from the
    // pot's top into its side (the pipe's own x turned onto z, then tilted).
    { id: 'worm', model: 'xsud 01.i3d', position: { x: -1744.0, y: 14.69, z: 1251.35 } },
    {
        id: 'pipe',
        model: 'xv trubka4.i3d',
        position: { x: -1744.0, y: 15.79, z: 1250.75 },
        rotation: { x: 0, y: -QUARTER + 0.374, z: -QUARTER },
    },
    // One vat in the stall's south end, one behind its counter.
    { id: 'vat1', model: 'tutsud.i3d', position: { x: -1744.0, y: 15.28, z: 1248.62 } },
    { id: 'vat2', model: 'tutsud.i3d', position: { x: -1745.45, y: 15.04, z: 1251.3 } },
    // Firewood stacked along the outside of the counter.
    {
        id: 'firewood',
        model: 'klady.i3d',
        position: { x: -1745.42, y: 14.64, z: 1249.0 },
        yaw: QUARTER,
        scale: 0.25,
    },
    // Water for the worm by the stall's north-east corner, and a bucket at
    // the worm's run.
    { id: 'water', model: 'xsud 01.i3d', position: { x: -1743.5, y: 14.68, z: 1252.95 } },
    { id: 'bucket', model: 'xkybl 1.i3d', position: { x: -1742.95, y: 14.82, z: 1251.55 } },
    // Hung from the roof over the gap between the pot and the open side,
    // its bottom 1.85 m above the ground there.
    { id: 'lamp', model: '9petrolejka.i3d', position: { x: -1743.45, y: 17.05, z: 1250.0 } },
    // The molasses: two sacks on the pallet, two more across them.
    {
        id: 'pallet',
        model: 'PALETA01.i3d',
        position: { x: -1744.6, y: 14.64, z: 1254.25 },
        yaw: 0.15,
    },
    {
        id: 'sack1',
        model: 'xpytel2.i3d',
        position: { x: -1744.55, y: 14.79, z: 1253.95 },
        yaw: 0.15,
    },
    {
        id: 'sack2',
        model: 'xpytel2.i3d',
        position: { x: -1744.65, y: 14.79, z: 1254.57 },
        yaw: 0.2,
    },
    { id: 'sack3', model: 'xpytel2.i3d', position: { x: -1744.6, y: 15.0, z: 1254.3 }, yaw: 0.35 },
    {
        id: 'sack4',
        model: 'xpytel2.i3d',
        position: { x: -1744.52, y: 15.21, z: 1254.15 },
        yaw: -0.1,
    },
    // The rack at CASKS: two rails along z 0.6 m apart, a cask for each
    // of MAX_CASKS lying across them.
    {
        id: 'rail1',
        model: 'tycka01.i3d',
        position: { x: -1742.0, y: 14.88, z: 1253.22 },
        rotation: { x: QUARTER, y: 0, z: 0 },
    },
    {
        id: 'rail2',
        model: 'tycka01.i3d',
        position: { x: -1741.4, y: 14.95, z: 1253.22 },
        rotation: { x: QUARTER, y: 0, z: 0 },
    },
    {
        id: 'cask1',
        model: 'sud01.i3d',
        position: { x: -1741.7, y: 15.28, z: 1253.61 },
        rotation: { x: 0, y: 0, z: CASK_TILT },
    },
    {
        id: 'cask2',
        model: 'sud01.i3d',
        position: { x: -1741.7, y: 15.28, z: 1254.27 },
        rotation: { x: 0, y: 0, z: CASK_TILT },
    },
    {
        id: 'cask3',
        model: 'sud01.i3d',
        position: { x: -1741.7, y: 15.28, z: 1254.93 },
        rotation: { x: 0, y: 0, z: CASK_TILT },
    },
    {
        id: 'cask4',
        model: 'sud01.i3d',
        position: { x: -1741.7, y: 15.28, z: 1255.59 },
        rotation: { x: 0, y: 0, z: CASK_TILT },
    },
    // The cases at CRATES: four on the ground, two on them, one on top.
    {
        id: 'crate1',
        model: 'bednakl.i3d',
        position: { x: -1747.37, y: 14.58, z: 1246.02 },
        yaw: -0.08,
    },
    {
        id: 'crate2',
        model: 'bednakl.i3d',
        position: { x: -1746.83, y: 14.68, z: 1246.02 },
        yaw: -0.03,
    },
    {
        id: 'crate3',
        model: 'bednakl.i3d',
        position: { x: -1747.37, y: 14.58, z: 1246.56 },
        yaw: 0.03,
    },
    {
        id: 'crate4',
        model: 'bednakl.i3d',
        position: { x: -1746.83, y: 14.67, z: 1246.56 },
        yaw: 0.08,
    },
    {
        id: 'crate5',
        model: 'bednakl.i3d',
        position: { x: -1747.37, y: 15.18, z: 1246.24 },
        yaw: 0.12,
    },
    {
        id: 'crate6',
        model: 'bednakl.i3d',
        position: { x: -1746.83, y: 15.18, z: 1246.31 },
        yaw: -0.08,
    },
    {
        id: 'crate7',
        model: 'bednakl.i3d',
        position: { x: -1747.1, y: 15.68, z: 1246.29 },
        yaw: 0.3,
    },
    // The packing bench along z east of the cases, 0.8 m off them so a man
    // fits between; bottles and a basket on it, a bale to sit on.
    {
        id: 'bench',
        model: 'xstul11.i3d',
        position: { x: -1745.3, y: 14.76, z: 1245.7 },
        yaw: QUARTER,
    },
    {
        id: 'bottle1',
        model: '2flask.i3d',
        position: { x: -1745.4, y: 15.46, z: 1246.25 },
        scale: 0.6,
    },
    {
        id: 'bottle2',
        model: '2flask.i3d',
        position: { x: -1745.18, y: 15.46, z: 1246.12 },
        scale: 0.6,
    },
    {
        id: 'bottle3',
        model: '2flask.i3d',
        position: { x: -1745.35, y: 15.46, z: 1245.4 },
        scale: 0.6,
    },
    {
        id: 'basket',
        model: 'kosik01.i3d',
        position: { x: -1745.35, y: 15.64, z: 1244.95 },
        yaw: 0.4,
    },
    {
        id: 'bale',
        model: 'xslama 1.i3d',
        position: { x: -1744.15, y: 14.91, z: 1245.35 },
        yaw: 0.25,
        scale: 0.8,
    },
];

// What the blast does to the camp; the rest stands. The burst pot is thrown
// off its hearth onto its side in the stall's open side, south of where the
// operator stands, the pipe is gone, the lamp lies on the ground, the bucket
// and the mash vat beside the pot are knocked over.
export const WRECKED: Readonly<Partial<Record<PropId, ObjectSpec | null>>> = {
    pot: {
        model: 'xv sud.i3d',
        position: { x: -1742.87, y: 15.37, z: 1248.54 },
        rotation: { x: QUARTER, y: 0.2, z: 0 },
        scale: 1.5,
    },
    pipe: null,
    lamp: {
        model: '9petrolejka.i3d',
        position: { x: -1743.1, y: 15.1, z: 1250.7 },
        rotation: { x: 0, y: 0.6, z: QUARTER },
    },
    bucket: {
        model: 'xkybl 1.i3d',
        position: { x: -1742.81, y: 15.02, z: 1251.73 },
        rotation: { x: QUARTER, y: 0.9, z: 0 },
    },
    vat1: {
        model: 'tutsud.i3d',
        position: { x: -1744.05, y: 15.16, z: 1248.75 },
        rotation: { x: QUARTER, y: 0, z: 0 },
    },
};
