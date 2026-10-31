// Where it is. The camp is the stock FREEKRAJINA scene2.bin's woodcutters'
// clearing: the shed "@bouda00" (models/@bouda06.4ds) at (-1735.92, 15.22,
// 1260.16), the shed "@bouda02" at (-1729.97, 15.91, 1239.16) and the log
// piles "klady7" (-1750.18, 14.56, 1256.02), "klady00" (-1739.65, 15.10,
// 1240.18), "klady03" (-1726.37, 15.21, 1268.30) and "klady8" (-1740.02,
// 14.51, 1277.88) (models/klady.4ds, 8.3 x 3.6 m), with the pole "Sloup43"
// at (-1739.61, 14.69, 1260.21). Freeride Extended's scene2.bin places the
// same sheds, log piles and pole at the same positions and heights. Ground
// heights below are sampled from the FREEKRAJINA scene.4ds ground mesh; the
// still and its gear stand in the open ground between the log piles, at
// least 5 m from any of them. The nearest road is the road graph line from
// crossing (-1850.7, -4.0, 1843.7) south past (-1610.3, 15.5, 1259.4),
// 130 m east, beyond a fenced pasture whose fences (plot3/plot4) run from
// (-1678, 1279) south-west to (-1734, 1225): cars reach the clearing round
// its north side. Stock FREEKRAJINA also has barrels here that the mod's
// scene lacks, so the distillery runs on Lost Heaven Extended only.
//
// What stands in the camp, and the ground under it, is camp.ts.
//
// Elsewhere: Ernesto is bars' sailor (sailor03, the MISE16-SALIERY frame at
// (-1778.47, -5.56, 19.44) in Salieri's billiard room, server/bars). The
// village store is the countryside main street crossing (-1484.1, 14.6,
// 725.1), on the road graph of both scenes (farm/); the hardware truck
// parks on the shoulder of the road graph line north out of that crossing
// (x -1484.1, z 733 to 840), 3.6 m east of it, where the scene.4ds ground
// is 14.44 in both scenes. Luigi's stop is the road graph crossing
// (-1781.1, -5.5, -8.2) by Salieri's (as bootleg.ts); Morello's is the road
// point (-404.0, 17.0, 440.3) 12 m from its door arrow (lib/maps.ts); Paco
// waits at the container docks crossing (lib/maps.ts).
import * as maps from '../lib/maps';
import { flatDistance, onStreet, type Point } from '../lib/util';

// The places around the still are chosen spots in the clearing's open
// ground, each given by its offset from the pot or from a scene2.bin object
// named in the header, at the height of the FREEKRAJINA scene.4ds ground
// mesh ("Object211") sampled there:
// - CASKS: the middle of the cask rack (camp.ts), 2.3 m east and 4.6 m
//   north of the pot; ground 14.90.
// - CRATES: the stack of cases (camp.ts), 3.1 m west and 3.7 m south of
//   the pot; ground 14.70.
// - YARD, where cars stop: the open ground east of the sheds toward the
//   pasture's north corner, 10.4 m south-east of the shed "@bouda00" and
//   14.3 m south of the log pile "klady03", with no scene2.bin object within
//   10 m; ground 15.50.
// - HANK_SPOT: 1.4 m east and 2.8 m south of the pot, 7.6 m from the log
//   pile "klady00"; ground 15.00.
// - REVEAL_CAMERA (below): 5.1 m south of "@bouda00" (ground 15.22), 3.3 m
//   up, looking at the pot 0.8 m above its base.
export const POT: Point = { x: -1744.0, y: 14.81, z: 1250.0 };
export const CASKS: Point = { x: -1741.7, y: 14.9, z: 1254.6 };
export const CRATES: Point = { x: -1747.1, y: 14.7, z: 1246.3 };
export const YARD: Point = { x: -1727.5, y: 15.5, z: 1254.0 };
export const HANK_SPOT: Point = { x: -1742.6, y: 15.0, z: 1247.2 };
// Open ground at the clearing's edges, 26 and 26 m from the still.
export const RAID_POINTS: readonly Point[] = [
    { x: -1715.0, y: 15.69, z: 1272.0 },
    { x: -1757.0, y: 14.58, z: 1272.0 },
];
export const SAILOR: Point = { x: -1778.47, y: -5.56, z: 19.44 };
// Salieri's front door (lib/maps.ts), where Luigi takes a bribe for the precinct.
export const SALIERI_DOOR: Point = maps.landmark('salieri').position;
export const VILLAGE: Point = { x: -1484.1, y: 14.6, z: 725.1 };
// On the shoulder north of the store (see the header); its driver smokes
// 2.7 m east of it, on the same ground (14.49).
export const TRUCK_SPOT: Point = { x: -1480.5, y: 14.5, z: 764.0 };
export const DRIVER_SPOT: Point = { x: -1477.8, y: 14.5, z: 762.0 };
export const REVEAL_CAMERA: { position: Point; target: Point } = {
    position: { x: -1733.0, y: 18.5, z: 1256.0 },
    target: { x: -1744.0, y: 15.6, z: 1250.0 },
};

/* Reach */

export const TOUCH = 2.2;
export const STILL_LEAVE = 4;
export const DISCOVER_RADIUS = 40;
export const YARD_RADIUS = 9;
export const STOP_RADIUS = 16;
export const SAILOR_REACH = 2.2;
export const TRUCK_REACH = 4.5;
export const DELIVER_COIL = 8;
export const CAMP_RADIUS = 60;
export const NEAR_CAMP = 300;
export const HANK_RANGE = 150;
export const RUMOUR_RADIUS = 900;

export function near(player: Player, point: Point, reach: number, level = 3): boolean {
    return (
        flatDistance(player.position, point) <= reach &&
        Math.abs(player.position.y - point.y) <= level
    );
}

export function camp(player: Player): boolean {
    return onStreet(player) && flatDistance(player.position, POT) <= CAMP_RADIUS;
}

/** The car the player drives, not one they ride in. */
export function driving(player: Player): Vehicle | null {
    return player.getSeat() === 0 ? player.getVehicle() : null;
}

/** Stopped there, as the yard and the village store want a car. */
export const STOPPED_SPEED = 1.5;
