// Yellow Pete's wall, his buy-back and where he stands
// (src/server/gunshop.ts): nothing bought from him sells back at a profit, he
// buys back only what he sold, nobody deals in a job's or a duel's guns, and
// he stands in his cellar clear of its walls and of the way down.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
    BUYBACK_SHARE,
    buyback,
    counterLines,
    dealFor,
    GUNS,
    receiptsFor,
    refusal,
    SIGN,
    SPOT,
    withReceipt,
} from '../src/server/gunshop';

// Rounds per magazine, the server's combat rules (stock predmety.def).
const MAGAZINE: Readonly<Record<number, number>> = {
    6: 6,
    7: 6,
    8: 6,
    9: 7,
    10: 50,
    11: 8,
    12: 2,
    13: 5,
    14: 5,
};

test('every piece is a distinct stock item, and no bat', () => {
    const ids = GUNS.map((gun) => gun.weaponId);

    assert.equal(new Set(ids).size, ids.length);
    assert.equal(new Set(GUNS.map((gun) => gun.id)).size, GUNS.length);
    assert.ok(ids.every((id) => id >= 2 && id <= 15));
    assert.ok(!ids.includes(4), 'every life starts with a bat');
});

test('a firearm comes loaded within its magazine, with rounds to buy for it', () => {
    for (const gun of GUNS.filter((entry) => entry.kind === 'firearm')) {
        assert.ok(gun.loaded <= MAGAZINE[gun.weaponId], gun.name);
        assert.ok(gun.ammo && gun.ammo.rounds > 0 && gun.ammo.price > 0, gun.name);
        assert.ok(gun.ammo.price < gun.price, `${gun.name}: rounds cost less than the gun`);
    }
});

test('Pete buys back only firearms, always for less than he charged', () => {
    for (const gun of GUNS) {
        const back = buyback(gun);

        if (gun.kind === 'firearm') {
            assert.ok(back > 0 && back <= gun.price * BUYBACK_SHARE, gun.name);
            assert.ok(back < gun.price, gun.name);
        } else {
            assert.equal(back, 0, gun.name);
        }
    }
});

test('a receipt is one sale back; none sells twice', () => {
    const gun = GUNS.find((entry) => entry.id === 'thompson')!;
    let receipts = withReceipt(undefined, gun.weaponId, 1);
    let paid = gun.price;
    let repaid = 0;

    assert.equal(receiptsFor(receipts, gun.weaponId), 1);

    // Rounds bought on top earn no receipt.
    paid += gun.ammo!.price;

    for (let attempt = 0; attempt < 3; attempt++) {
        if (receiptsFor(receipts, gun.weaponId) > 0) {
            repaid += buyback(gun);
            receipts = withReceipt(receipts, gun.weaponId, -1);
        }
    }

    assert.equal(repaid, buyback(gun));
    assert.ok(repaid < paid);
    assert.deepEqual(receipts, {});
    assert.deepEqual(withReceipt({}, gun.weaponId, -1), {});
});

test('the card shows rounds for what is carried and sells only his own', () => {
    const colt = GUNS.find((entry) => entry.id === 'colt-ds')!;
    const tommy = GUNS.find((entry) => entry.id === 'thompson')!;
    const lines = counterLines(
        [4, colt.weaponId, tommy.weaponId],
        tommy.weaponId,
        withReceipt(undefined, colt.weaponId, 1),
    );
    const ids = lines.map((line) => line.id);

    assert.ok(ids.includes('colt-ds:ammo') && !ids.includes('colt-ds'));
    assert.ok(ids.includes('thompson:ammo') && !ids.includes('thompson'));
    assert.ok(ids.includes('shotgun'));

    const sales = lines.filter((line) => line.sell);

    // The gun in hand first; the bat is not his.
    assert.deepEqual(
        sales.map((line) => line.id),
        ['thompson:sell', 'colt-ds:sell'],
    );

    assert.equal(sales[0].price, 0, 'he did not sell this Thompson');
    assert.equal(sales[1].price, buyback(colt));

    for (const id of ids) {
        assert.ok(dealFor(id), id);
    }
});

test('only real lines name a deal', () => {
    assert.deepEqual(dealFor('knife')?.kind, 'gun');
    assert.equal(dealFor('knife:sell'), null);
    assert.equal(dealFor('grenades:ammo'), null);
    assert.equal(dealFor('grenades:sell'), null);
    assert.equal(dealFor('bat'), null);
    assert.equal(dealFor('thompson:free'), null);
});

test('no deals in a uniform, a duel, a cell or a hold-up', () => {
    for (const kind of ['duty', 'fire', 'duel', 'jail', 'visit', 'robbery'] as const) {
        assert.ok(refusal(kind), kind);
    }

    for (const kind of ['courier', 'race', 'taxi', 'medic', 'farm', null] as const) {
        assert.equal(refusal(kind), null, String(kind));
    }
});

test('Pete stands in his cellar, clear of the walls and the way down', () => {
    // "&& hala" of the city scene, and the stair door "&& prah01".
    const { x, y, z } = SPOT.position;

    assert.ok(x > 68.53 + 1 && x < 74.53 - 1 && z > 125.78 + 1 && z < 137.28 - 1);
    assert.equal(y, 2.01);
    assert.ok(Math.hypot(x - 71.54, z - 125.53) > 6, 'away from the stair door');
    // Facing up the cellar toward the stairs (-z).
    assert.ok(Math.cos(SPOT.heading) < -0.99);
    // His name hangs over the street door (Box24), 25 m from him.
    assert.ok(Math.hypot(SIGN.x - 61.13, SIGN.z - 113.83) < 1);
});
