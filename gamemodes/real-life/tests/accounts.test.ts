import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';
import { test } from 'node:test';
import { DatabaseSync } from 'node:sqlite';
import { Accounts } from '../src/server/accounts';
import { hashPassword, verifyPassword } from '../src/server/password';
import { RateLimit } from '../src/server/rate-limit';
import { credentialsError } from '../src/shared/protocol';
import en from '../src/shared/locales/en.json';
import cs from '../src/shared/locales/cs.json';

test('SQLite survives reopening and enforces case-insensitive account uniqueness', () => {
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const folder = mkdtempSync(resolve(parent, 'accounts-'));
    const path = resolve(folder, 'accounts.sqlite');
    let store = new Accounts(path);

    try {
        const account = store.create('Angelo', 'test-hash', 'cs')!;

        assert.equal(store.create('ANGELO', 'different-hash', 'en'), undefined);
        const place = { mission: 'freeride', position: { x: -812, y: 4, z: 156 }, heading: 1.25 };

        store.save(account.id, 'Paulie.i3d', 'cs', place);
        store.close();
        store = new Accounts(path);
        const restored = store.find('angelo')!;

        assert.equal(restored.username, 'Angelo');
        assert.equal(restored.model, 'Paulie.i3d');
        assert.equal(restored.locale, 'cs');
        assert.deepEqual(restored.place, place);
        assert.equal(store.find("' OR 1=1 --"), undefined);
    } finally {
        store.close();
        rmSync(folder, { recursive: true });
    }
});

test('passwords are salted and verified without storing plaintext', async () => {
    const password = 'A sufficiently long phrase';
    const first = await hashPassword(password);
    const second = await hashPassword(password);

    assert.notEqual(first, second);
    assert.equal(first.includes(password), false);
    assert.equal(await verifyPassword(password, first), true);
    assert.equal(await verifyPassword('wrong password', first), false);
    assert.equal(await verifyPassword(password, undefined), false);
    assert.equal(await verifyPassword(password, 'invalid-hash'), false);
});

test('existing databases migrate to user ranks and retain ranks and bans across restarts', () => {
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const folder = mkdtempSync(resolve(parent, 'roles-'));
    const path = resolve(folder, 'accounts.sqlite');
    let store = new Accounts(path);

    try {
        const admin = store.create('Admin', 'hash', 'en')!;
        const user = store.create('Angelo', 'hash', 'cs')!;
        const remembered = store.remember(user);

        store.close();
        const legacy = new DatabaseSync(path);

        legacy.exec('ALTER TABLE accounts DROP COLUMN role');
        legacy.close();
        store = new Accounts(path);
        assert.equal(store.find('Admin')?.role, 'user');
        assert.equal(store.recall(remembered.token)?.id, user.id);
        store.setRole(admin.id, 'admin');
        store.ban(user.id, 'Saved reason', admin.id);
        store.close();
        store = new Accounts(path);
        assert.equal(store.find('Admin')?.role, 'admin');
        assert.equal(store.getBan(user.id)?.reason, 'Saved reason');
        assert.equal(store.recall(remembered.token), undefined);
        assert.equal(store.unban(user.id), true);
        assert.equal(store.unban(user.id), false);
        const inspect = new DatabaseSync(path);

        inspect.prepare('UPDATE accounts SET role = ? WHERE id = ?').run('unknown', user.id);
        inspect.close();
        assert.equal(store.find('Angelo')?.role, 'user');
        assert.equal(store.create('NewUser', 'hash', 'en')?.role, 'user');
    } finally {
        store.close();
        rmSync(folder, { recursive: true });
    }
});

test('remembered sign-ins survive reopening, expire and can be revoked', () => {
    const parent = resolve('../../build/real-life-tests');

    mkdirSync(parent, { recursive: true });
    const folder = mkdtempSync(resolve(parent, 'remember-'));
    const path = resolve(folder, 'accounts.sqlite');
    let store = new Accounts(path);

    try {
        const account = store.create('Angelo', 'test-hash', 'en')!;
        const remembered = store.remember(account, 1000);
        const another = store.remember(account, 1000);
        const inspect = new DatabaseSync(path);

        assert.equal(
            inspect
                .prepare('SELECT count(*) AS count FROM remembered_accounts WHERE token_hash = ?')
                .get(remembered.token)!.count,
            0,
        );

        inspect.close();
        assert.notEqual(remembered.token, another.token);
        assert.equal(store.recall(remembered.token, 1001)?.id, account.id);
        assert.equal(store.recall(remembered.token, remembered.expiresAt), undefined);
        assert.equal(store.recall('0'.repeat(64), 1001), undefined);
        store.close();
        store = new Accounts(path);
        assert.equal(store.serverId, remembered.serverId);
        assert.equal(store.recall(remembered.token, 1001)?.username, 'Angelo');
        store.forget(remembered.token);
        assert.equal(store.recall(remembered.token, 1001), undefined);
        assert.equal(store.recall(another.token, 1001)?.id, account.id);
    } finally {
        store.close();
        rmSync(folder, { recursive: true });
    }
});

test('credentials reject invalid names, oversized passwords and unknown actions', () => {
    const valid = {
        username: 'Angelo_01',
        password: 'long-password',
        mode: 'register',
        locale: 'en',
    };

    assert.equal(credentialsError(valid), null);
    assert.equal(credentialsError({ ...valid, username: '<script>' }), 'invalidUsername');
    assert.equal(credentialsError({ ...valid, password: 'x'.repeat(129) }), 'invalidPassword');
    assert.equal(credentialsError({ ...valid, mode: 'spawn' }), 'unavailable');
    assert.equal(credentialsError({ ...valid, locale: 'unknown' }), 'unavailable');
    assert.equal(credentialsError({ ...valid, remember: 'true' }), 'unavailable');
    assert.equal(
        credentialsError({
            ...valid,
            mode: 'remembered',
            password: undefined,
            token: 'a'.repeat(64),
        }),
        null,
    );

    assert.equal(
        credentialsError({ ...valid, mode: 'remembered', token: 'invalid' }),
        'rememberedExpired',
    );
});

test('rate limits expire and keep separate clients independent', () => {
    const limit = new RateLimit(2, 100);

    assert.equal(limit.take('one', 0), true);
    assert.equal(limit.take('one', 1), true);
    assert.equal(limit.take('one', 2), false);
    assert.equal(limit.take('two', 2), true);
    assert.equal(limit.take('one', 100), true);
    limit.prune(202);
    assert.equal(limit.take('two', 203), true);
});

test('English and Czech have identical translation keys and no empty strings', () => {
    function keys(value: object, prefix = ''): string[] {
        return Object.entries(value)
            .flatMap(([key, child]) => {
                if (typeof child === 'object') {
                    return keys(child, `${prefix}${key}.`);
                }

                assert.equal(typeof child, 'string');
                assert.ok(child.length > 0);

                return [`${prefix}${key}`];
            })
            .sort();
    }

    assert.deepEqual(keys(en), keys(cs));
});
