// Everything Free Ride keeps between sessions, in one SQLite database
// (data/freeride/freeride.db, through the server's built-in node:sqlite).
// Player records are one row each, with their main figures as columns for
// queries; every other module stores one JSON document (crews, races, turf
// and so on). Each save is its own transaction, so a crash loses nothing that
// was saved. The JSON files of earlier versions are imported once and left
// in place.
import * as fs from 'node:fs';
import * as path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import * as log from './log';

export const DATA_DIR = path.join(process.cwd(), 'data', 'freeride');
export const FILE = path.join(DATA_DIR, 'freeride.db');

fs.mkdirSync(DATA_DIR, { recursive: true });
const db = new DatabaseSync(FILE);

db.exec(`
    PRAGMA journal_mode = WAL;
    PRAGMA synchronous = NORMAL;
    CREATE TABLE IF NOT EXISTS players (
        key TEXT PRIMARY KEY,
        data TEXT NOT NULL,
        name TEXT GENERATED ALWAYS AS (json_extract(data, '$.name')) VIRTUAL,
        money INTEGER GENERATED ALWAYS AS (json_extract(data, '$.money')) VIRTUAL,
        xp INTEGER GENERATED ALWAYS AS (json_extract(data, '$.xp')) VIRTUAL,
        kills INTEGER GENERATED ALWAYS AS (json_extract(data, '$.kills')) VIRTUAL,
        deaths INTEGER GENERATED ALWAYS AS (json_extract(data, '$.deaths')) VIRTUAL,
        last_seen INTEGER GENERATED ALWAYS AS (json_extract(data, '$.lastSeen')) VIRTUAL
    );
    CREATE TABLE IF NOT EXISTS documents (name TEXT PRIMARY KEY, data TEXT NOT NULL, updated_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS imports (name TEXT PRIMARY KEY, at INTEGER NOT NULL);
`);

const selectPlayers = db.prepare('SELECT key, data FROM players');
const upsertPlayer = db.prepare(
    'INSERT INTO players (key, data) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET data = excluded.data',
);
const selectDocument = db.prepare('SELECT data FROM documents WHERE name = ?');
const upsertDocument = db.prepare(
    'INSERT INTO documents (name, data, updated_at) VALUES (?, ?, ?) ON CONFLICT (name) DO UPDATE SET data = excluded.data, updated_at = excluded.updated_at',
);
const selectImport = db.prepare('SELECT 1 FROM imports WHERE name = ?');
const insertImport = db.prepare('INSERT OR IGNORE INTO imports (name, at) VALUES (?, ?)');

function transaction(work: () => void): void {
    db.exec('BEGIN');

    try {
        work();
        db.exec('COMMIT');
    } catch (caught) {
        db.exec('ROLLBACK');
        throw caught;
    }
}

function isMissing(caught: unknown): boolean {
    return caught instanceof Error && 'code' in caught && caught.code === 'ENOENT';
}

// The contents of an earlier version's data/freeride/<name>.json, once.
function legacy<T>(name: string): T | null {
    if (selectImport.get(name)) {
        return null;
    }

    insertImport.run(name, Date.now());
    const file = path.join(DATA_DIR, `${name}.json`);

    try {
        const value: T = JSON.parse(fs.readFileSync(file, 'utf8'));

        log.info(`Imported ${file} into ${FILE}`);

        return value;
    } catch (caught) {
        if (!isMissing(caught)) {
            log.error(`Could not import ${file}: ${log.describe(caught)}`);
        }

        return null;
    }
}

/** Every player record by key. A record is what lib/profiles.ts saved, so it is that module's to read. */
export function loadPlayers<T>(): Map<string, T> {
    const players = new Map<string, T>();

    for (const row of selectPlayers.all()) {
        if (typeof row.key === 'string' && typeof row.data === 'string') {
            players.set(row.key, JSON.parse(row.data));
        }
    }

    const imported = legacy<{ players?: Record<string, T> }>('profiles');

    if (imported?.players) {
        for (const [key, record] of Object.entries(imported.players)) {
            if (!players.has(key)) {
                players.set(key, record);
            }
        }

        savePlayers(players);
    }

    return players;
}

export function savePlayers(entries: Iterable<[string, unknown]>): void {
    try {
        transaction(() => {
            for (const [key, record] of entries) {
                upsertPlayer.run(key, JSON.stringify(record));
            }
        });
    } catch (caught) {
        log.error(`Could not save player records: ${log.describe(caught)}`);
    }
}

/**
 * A module's saved document, or null when it has none yet. It is what that
 * module saved (or an earlier version of it did): validate what it may lack.
 */
export function load<T>(name: string): T | null {
    const row = selectDocument.get(name);

    if (row && typeof row.data === 'string') {
        return JSON.parse(row.data);
    }

    const imported = legacy<T>(name);

    if (imported) {
        save(name, imported);
    }

    return imported;
}

export function save(name: string, value: unknown): void {
    try {
        upsertDocument.run(name, JSON.stringify(value), Date.now());
    } catch (caught) {
        log.error(`Could not save ${name}: ${log.describe(caught)}`);
    }
}
