// Yellow Pete's wall: the guns he sells, the ammunition for the ones a
// customer already carries, and what he pays to take one back. Pure rules;
// server/shops.ts charges, hands over and takes back, and server/pete.ts is
// the man himself.
//
// The stock Free Ride sold these from the same cellar: each weapon model on
// its walls (FREERIDE and FREERIDENOC scene2.bin "2coltds", "2sw2", "2c",
// "2sw", "2sawoff", "2shotgun", "2tommy", "2m", "2mosin", "2knife", "2mol",
// "2grenade", ...) runs a script that waits for the use key, takes
// freeride_scoreget money and gives human_addweapon 0, <id>, <loaded>,
// <reserve>; the dol10 ... dol2000 price tags hang beside them. Stock
// prices: knife, bat and Colt Detective Special $10, S&W Model 10 $30, Colt
// 1911 $50, S&W Model 27 $100, both shotguns $500, Thompson, grenade and
// Molotov $1000, Springfield and Mosin-Nagant $2000. Pete keeps that order
// at Free Ride's prices (a day's odd jobs pay $30 an hour), and the stock
// loads where Free Ride sold the gun before (the shotgun, Thompson and
// Colt kept their old loads and prices). He sells no bat: every life starts
// with one.
import type { ActivityKind } from './lib/life';

/** A world position (lib/util.ts Point; this module stays free of the runtime). */
interface Point {
    x: number;
    y: number;
    z: number;
}

// The cellar is the "&& hala" sector of the city scene (FREERIDE and
// FREERIDENOC scene.4ds; Lost Heaven Extended's freeride_extended scene.4ds
// has the same sectors at the same place): x 68.53..74.53, z
// 125.78..137.28, floor y 2.01, in three bays under arches at z 128.9 and
// 132.7. The way down from the street: the door Box24 (its leaf hinged at
// 60.58, 6.09, 113.83; unlocked in both scenes' scene2.bin), the corridor
// "&& chodba" (floor 4.86) east to the grating "mrize dve 08" (open in
// scene2.bin), and the stairs "&& schodiste" down south to the cellar's
// door frame at z 125.5. Pete used to stand on the street at the stock
// frame "1dvere u peteho" (MISE19-MESTO scene2.bin: 61.14, 6.12, 113.52),
// right in front of that door.
//
// In mission 19 (MISE19-MESTO scene2.bin) Pete, "1ProdZ" (models
// prodz1.4ds), walks to web point 9 of the mission's check.bin (71.495,
// 2.125, 133.527), just inside the back bay's arch, and looks at point 10
// (71.587, 2.125, 127.915), up the cellar toward the stairs. Pete stands
// there now, on the cellar floor (web points ride 0.115 m above it). The Free
// Ride scenes have no counter there (MISE19's Box27 across the arch is not in
// them) and nothing stands on the floor within a metre of the spot: the
// arch's posts (gBox07, gLoft05) leave it open from x 70.04 to 73.05, the
// stair door is 8 m away, and Lost Heaven Extended's scene2.bin adds only
// two barrels in the front bays (bochka, bochkab).
const WEB_POINT_9: Point = { x: 71.495, y: 2.01, z: 133.527 };
const WEB_POINT_10: Point = { x: 71.587, y: 2.01, z: 127.915 };

/** Where Pete stands (server/pete.ts) and his counter is (server/shops.ts), facing up the cellar. */
export const SPOT = {
    position: WEB_POINT_9,
    heading: Math.atan2(WEB_POINT_10.x - WEB_POINT_9.x, WEB_POINT_10.z - WEB_POINT_9.z),
} as const;

/** Over the street door (Box24, top 7.22), out on the pavement: where his name hangs. */
export const SIGN: Point = { x: 61.13, y: 7.7, z: 113.4 };

/** What Pete takes back of his price for a gun he sold: never a profit. */
export const BUYBACK_SHARE = 0.4;

export type GunKind = 'melee' | 'firearm' | 'throwable';

/** A piece on Pete's wall. */
export interface Gun {
    /** Its line on the counter card. */
    id: string;
    /** Stock item id (Weapon). */
    weaponId: number;
    name: string;
    kind: GunKind;
    /** The piece with its load. */
    price: number;
    /** Rounds in it and spare; a throwable's count is its loaded value. */
    loaded: number;
    reserve: number;
    /** A box of rounds for a gun the customer already carries. */
    ammo?: { rounds: number; price: number };
    note: string;
}

export const GUNS: readonly Gun[] = [
    {
        id: 'knife',
        weaponId: 3,
        name: 'Knife',
        kind: 'melee',
        price: 30,
        loaded: 1,
        reserve: 0,
        note: 'Quiet, and it never jams',
    },
    {
        id: 'colt-ds',
        weaponId: 6,
        name: 'Colt Detective Special',
        kind: 'firearm',
        price: 90,
        loaded: 6,
        reserve: 18,
        ammo: { rounds: 18, price: 25 },
        note: 'Snub-nosed .38 · six in the chamber, eighteen spare',
    },
    {
        id: 'sw-mp',
        weaponId: 8,
        name: 'S&W Model 10 M&P',
        kind: 'firearm',
        price: 110,
        loaded: 6,
        reserve: 18,
        ammo: { rounds: 18, price: 30 },
        note: "A policeman's revolver, longer barrel",
    },
    {
        id: 'colt-1911',
        weaponId: 9,
        name: 'Colt 1911',
        kind: 'firearm',
        price: 140,
        loaded: 7,
        reserve: 21,
        ammo: { rounds: 21, price: 35 },
        note: 'Army .45 · seven in the clip, twenty-one spare',
    },
    {
        id: 'magnum',
        weaponId: 7,
        name: 'S&W Model 27 Magnum',
        kind: 'firearm',
        price: 180,
        loaded: 6,
        reserve: 18,
        ammo: { rounds: 18, price: 45 },
        note: 'Hits like a truck',
    },
    {
        id: 'sawed-off',
        weaponId: 12,
        name: 'Sawed-off shotgun',
        kind: 'firearm',
        price: 190,
        loaded: 2,
        reserve: 22,
        ammo: { rounds: 20, price: 45 },
        note: 'Two barrels, fits under a coat',
    },
    {
        id: 'shotgun',
        weaponId: 11,
        name: 'Pump-action shotgun',
        kind: 'firearm',
        price: 210,
        loaded: 8,
        reserve: 24,
        ammo: { rounds: 24, price: 55 },
        note: 'For when a quiet word will not do',
    },
    {
        id: 'thompson',
        weaponId: 10,
        name: 'Thompson 1928',
        kind: 'firearm',
        price: 320,
        loaded: 50,
        reserve: 100,
        ammo: { rounds: 100, price: 90 },
        note: 'A loud answer to a bad question',
    },
    {
        id: 'mosin',
        weaponId: 14,
        name: 'Mosin-Nagant 1891/30',
        kind: 'firearm',
        price: 380,
        loaded: 5,
        reserve: 10,
        ammo: { rounds: 10, price: 60 },
        note: 'Russian, bolt-action, settles it at a distance',
    },
    {
        id: 'springfield',
        weaponId: 13,
        name: 'US Rifle M1903 Springfield',
        kind: 'firearm',
        price: 380,
        loaded: 5,
        reserve: 15,
        ammo: { rounds: 15, price: 70 },
        note: 'Army rifle, five in the magazine',
    },
    {
        id: 'molotovs',
        weaponId: 5,
        name: 'Three Molotov cocktails',
        kind: 'throwable',
        price: 95,
        loaded: 3,
        reserve: 0,
        note: 'Light the rag, then throw',
    },
    {
        id: 'grenades',
        weaponId: 15,
        name: 'Three grenades',
        kind: 'throwable',
        price: 95,
        loaded: 3,
        reserve: 0,
        note: 'Keep them out of the glovebox',
    },
];

/** A line on Pete's counter card; server/shops.ts sends it as an Offer. */
export interface CounterLine {
    id: string;
    name: string;
    price: number;
    note: string;
    /** Pete pays this; 0 when he will not take it. */
    sell?: boolean;
}

/** What a line on the card does. */
export type Deal =
    { kind: 'gun'; gun: Gun } | { kind: 'ammo'; gun: Gun } | { kind: 'sell'; gun: Gun };

/** Guns Pete sold a player and has not bought back, by weapon id. */
export type Receipts = Readonly<Record<string, number>>;

/** What Pete pays for a gun of his own: only firearms, never what he charged. */
export function buyback(gun: Gun): number {
    return gun.kind === 'firearm' ? Math.floor(gun.price * BUYBACK_SHARE) : 0;
}

export function receiptsFor(receipts: Receipts | undefined, weaponId: number): number {
    return Math.max(0, Math.floor(receipts?.[String(weaponId)] ?? 0));
}

/** The receipts after `delta` more (or fewer) guns of this kind; none drop below zero. */
export function withReceipt(
    receipts: Receipts | undefined,
    weaponId: number,
    delta: number,
): Record<string, number> {
    const next: Record<string, number> = { ...receipts };
    const count = receiptsFor(receipts, weaponId) + delta;

    if (count > 0) {
        next[String(weaponId)] = count;
    } else {
        delete next[String(weaponId)];
    }

    return next;
}

// While these hold a player, what they carry is the job's or the duel's
// (lib/shift.ts gives the old loadout back when a uniform comes off, and a
// duel puts the old one back too): a gun bought now would be swapped away,
// and a gun sold now was never theirs to sell. The jail and a visit leave
// nothing to deal with; a hold-up is no time to shop.
const NO_DEAL: Partial<Record<ActivityKind, string>> = {
    duty: 'Not to a man in a police uniform. Come back in your own clothes.',
    fire: "Not in the fire brigade's coat. Come back off duty.",
    duel: 'Settle your duel first. Then we talk.',
    jail: 'Not now.',
    visit: 'Not now.',
    robbery: 'You hold up my counter and want to shop? Get out.',
};

/** What Pete says to someone busy with `activity` instead of dealing, or null when he deals. */
export function refusal(activity: ActivityKind | null | undefined): string | null {
    return (activity && NO_DEAL[activity]) ?? null;
}

/**
 * Pete's card for one customer: every piece on the wall (a box of rounds in
 * place of a firearm they carry already), then one line per firearm of his
 * they carry, to sell; the one in hand first. A firearm he did not sell them
 * shows with no price: he will not take it.
 */
export function counterLines(
    held: readonly number[],
    selected: number,
    receipts: Receipts | undefined,
): CounterLine[] {
    const carries = new Set(held);
    const lines: CounterLine[] = GUNS.map((gun) =>
        gun.kind === 'firearm' && gun.ammo && carries.has(gun.weaponId)
            ? {
                  id: `${gun.id}:ammo`,
                  name: `${gun.name} rounds`,
                  price: gun.ammo.price,
                  note: `${gun.ammo.rounds} more for the one you carry`,
              }
            : { id: gun.id, name: gun.name, price: gun.price, note: gun.note },
    );
    const forSale = GUNS.filter((gun) => gun.kind === 'firearm' && carries.has(gun.weaponId)).sort(
        (a, b) => Number(b.weaponId === selected) - Number(a.weaponId === selected),
    );

    for (const gun of forSale) {
        const his = receiptsFor(receipts, gun.weaponId) > 0;

        lines.push({
            id: `${gun.id}:sell`,
            name: `Sell your ${gun.name}`,
            price: his ? buyback(gun) : 0,
            note: his
                ? `Pete takes back his own at ${Math.round(BUYBACK_SHARE * 100)}% · the rounds in it go with it`
                : 'Not from his wall: Pete will not touch it',
            sell: true,
        });
    }

    return lines;
}

/** The deal a line id names, or null for none of Pete's. */
export function dealFor(offerId: string): Deal | null {
    const [id, part] = offerId.split(':');
    const gun = GUNS.find((entry) => entry.id === id);

    if (!gun) {
        return null;
    }

    if (part === undefined) {
        return { kind: 'gun', gun };
    }

    if (part === 'ammo' && gun.ammo) {
        return { kind: 'ammo', gun };
    }

    return part === 'sell' && gun.kind === 'firearm' ? { kind: 'sell', gun } : null;
}
