import { DatabaseSync } from 'node:sqlite';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import type { Locale, RememberedAccount, SavedPlace } from '../shared/protocol';
import { isRole, type Role } from '../shared/permissions';

const rememberMs = 30 * 24 * 60 * 60 * 1000;
const tokenHash = (token: string) => createHash('sha256').update(token).digest('hex');

export interface Account {
    id: number;
    username: string;
    passwordHash: string;
    locale: Locale;
    model: string | null;
    place: SavedPlace | null;
    role: Role;
}
interface AccountRow {
    id: number;
    username: string;
    password_hash: string;
    locale: Locale;
    model: string | null;
    mission: string | null;
    x: number | null;
    y: number | null;
    z: number | null;
    heading: number | null;
    role: string;
}

export class Accounts {
    private readonly db: DatabaseSync;
    readonly serverId: string;

    constructor(path: string) {
        this.db = new DatabaseSync(path);
        this.db.exec(`
            PRAGMA journal_mode = WAL;
            PRAGMA busy_timeout = 3000;
            PRAGMA foreign_keys = ON;
            CREATE TABLE IF NOT EXISTS accounts (
                id INTEGER PRIMARY KEY,
                username TEXT NOT NULL COLLATE NOCASE UNIQUE,
                password_hash TEXT NOT NULL,
                locale TEXT NOT NULL CHECK (locale IN ('en', 'cs')),
                model TEXT,
                mission TEXT,
                x REAL, y REAL, z REAL, heading REAL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            ) STRICT;
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            ) STRICT;
            CREATE TABLE IF NOT EXISTS remembered_accounts (
                token_hash TEXT PRIMARY KEY,
                account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                expires_at INTEGER NOT NULL
            ) STRICT;
            CREATE TABLE IF NOT EXISTS bans (
                account_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
                reason TEXT NOT NULL,
                banned_by INTEGER REFERENCES accounts(id),
                created_at INTEGER NOT NULL
            ) STRICT;
        `);

        const columns = this.db.prepare('PRAGMA table_info(accounts)').all();

        if (!columns.some((column) => column.name === 'role')) {
            this.db.exec("ALTER TABLE accounts ADD COLUMN role TEXT NOT NULL DEFAULT 'user'");
        }

        this.db
            .prepare('INSERT OR IGNORE INTO settings (key, value) VALUES (?, ?)')
            .run('server_id', randomUUID());

        this.serverId = this.db
            .prepare('SELECT value FROM settings WHERE key = ?')
            .get('server_id')!.value as string;
    }

    find(username: string): Account | undefined {
        const row = this.db
            .prepare('SELECT * FROM accounts WHERE username = ?')
            .get(username) as unknown as AccountRow | undefined;

        if (!row) {
            return undefined;
        }

        return {
            id: row.id,
            username: row.username,
            passwordHash: row.password_hash,
            locale: row.locale,
            model: row.model,
            role: isRole(row.role) ? row.role : 'user',
            place:
                row.mission !== null &&
                row.x !== null &&
                row.y !== null &&
                row.z !== null &&
                row.heading !== null
                    ? {
                          mission: row.mission,
                          position: { x: row.x, y: row.y, z: row.z },
                          heading: row.heading,
                      }
                    : null,
        };
    }

    create(username: string, passwordHash: string, locale: Locale): Account | undefined {
        // The unique index also handles two registrations finishing together.
        const result = this.db
            .prepare(
                'INSERT INTO accounts (username, password_hash, locale) VALUES (?, ?, ?) ON CONFLICT(username) DO NOTHING',
            )
            .run(username, passwordHash, locale);

        return result.changes ? this.find(username) : undefined;
    }

    save(id: number, model: string, locale: Locale, place: SavedPlace): void {
        this.db
            .prepare(
                `UPDATE accounts SET model = ?, locale = ?, mission = ?,
            x = ?, y = ?, z = ?, heading = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`,
            )
            .run(
                model,
                locale,
                place.mission,
                place.position.x,
                place.position.y,
                place.position.z,
                place.heading,
                id,
            );
    }

    close(): void {
        this.db.close();
    }

    remember(account: Account, now = Date.now()): RememberedAccount {
        const token = randomBytes(32).toString('hex');
        const expiresAt = now + rememberMs;

        this.db.prepare('DELETE FROM remembered_accounts WHERE expires_at <= ?').run(now);
        this.db
            .prepare(
                'INSERT INTO remembered_accounts (token_hash, account_id, expires_at) VALUES (?, ?, ?)',
            )
            .run(tokenHash(token), account.id, expiresAt);

        return { serverId: this.serverId, username: account.username, token, expiresAt };
    }

    recall(token: string, now = Date.now()): Account | undefined {
        const row = this.db
            .prepare(
                `SELECT username FROM accounts JOIN remembered_accounts
                ON accounts.id = remembered_accounts.account_id
                WHERE token_hash = ? AND expires_at > ?`,
            )
            .get(tokenHash(token), now);

        return row ? this.find(row.username as string) : undefined;
    }

    forget(token: string): void {
        this.db
            .prepare('DELETE FROM remembered_accounts WHERE token_hash = ?')
            .run(tokenHash(token));
    }

    setRole(id: number, role: Role): void {
        this.db.prepare('UPDATE accounts SET role = ? WHERE id = ?').run(role, id);
    }

    getBan(id: number): { reason: string } | undefined {
        return this.db.prepare('SELECT reason FROM bans WHERE account_id = ?').get(id) as
            { reason: string } | undefined;
    }

    ban(id: number, reason: string, bannedBy: number): void {
        this.db.exec('BEGIN');

        try {
            this.db
                .prepare(
                    `INSERT INTO bans (account_id, reason, banned_by, created_at)
                VALUES (?, ?, ?, ?) ON CONFLICT(account_id) DO UPDATE SET
                reason = excluded.reason, banned_by = excluded.banned_by, created_at = excluded.created_at`,
                )
                .run(id, reason, bannedBy, Date.now());

            this.db.prepare('DELETE FROM remembered_accounts WHERE account_id = ?').run(id);
            this.db.exec('COMMIT');
        } catch (error) {
            this.db.exec('ROLLBACK');

            throw error;
        }
    }

    unban(id: number): boolean {
        return this.db.prepare('DELETE FROM bans WHERE account_id = ?').run(id).changes > 0;
    }
}
