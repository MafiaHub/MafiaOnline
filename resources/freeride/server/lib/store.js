// Everything Free Ride keeps between sessions, in one SQLite database
// (data/freeride/freeride.db, through the server's built-in node:sqlite).
// Player records are one row each, with their main figures as columns for
// queries; every other module stores one JSON document (crews, races, turf
// and so on). Each save is its own transaction, so a crash loses nothing that
// was saved. The JSON files of earlier versions are imported once and left
// in place.
const fs = require("node:fs");
const path = require("node:path");
const {DatabaseSync} = require("node:sqlite");

const DATA_DIR = path.join(process.cwd(), "data", "freeride");
const FILE = path.join(DATA_DIR, "freeride.db");

fs.mkdirSync(DATA_DIR, {recursive: true});
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

const selectPlayers = db.prepare("SELECT key, data FROM players");
const upsertPlayer = db.prepare("INSERT INTO players (key, data) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET data = excluded.data");
const selectDocument = db.prepare("SELECT data FROM documents WHERE name = ?");
const upsertDocument = db.prepare("INSERT INTO documents (name, data, updated_at) VALUES (?, ?, ?) ON CONFLICT (name) DO UPDATE SET data = excluded.data, updated_at = excluded.updated_at");
const selectImport = db.prepare("SELECT 1 FROM imports WHERE name = ?");
const insertImport = db.prepare("INSERT OR IGNORE INTO imports (name, at) VALUES (?, ?)");

function transaction(work) {
    db.exec("BEGIN");
    try {
        work();
        db.exec("COMMIT");
    } catch (error) {
        db.exec("ROLLBACK");
        throw error;
    }
}

// The contents of an earlier version's data/freeride/<name>.json, once.
function legacy(name) {
    if (selectImport.get(name)) return null;
    insertImport.run(name, Date.now());
    const file = path.join(DATA_DIR, `${name}.json`);
    try {
        const value = JSON.parse(fs.readFileSync(file, "utf8"));
        console.log(`[freeride] Imported ${file} into ${FILE}`);
        return value;
    } catch (error) {
        if (error.code !== "ENOENT") console.error(`[freeride] Could not import ${file}: ${error.message}`);
        return null;
    }
}

// key -> record
function loadPlayers() {
    const players = new Map(selectPlayers.all().map((row) => [row.key, JSON.parse(row.data)]));
    const imported = legacy("profiles");
    if (imported?.players) {
        for (const [key, record] of Object.entries(imported.players)) if (!players.has(key)) players.set(key, record);
        savePlayers(players);
    }
    return players;
}

function savePlayers(entries) {
    try {
        transaction(() => {
            for (const [key, record] of entries) upsertPlayer.run(key, JSON.stringify(record));
        });
    } catch (error) {
        console.error(`[freeride] Could not save player records: ${error.message}`);
    }
}

// A module's saved document, or null when it has none yet.
function load(name) {
    const row = selectDocument.get(name);
    if (row) return JSON.parse(row.data);
    const imported = legacy(name);
    if (imported) save(name, imported);
    return imported;
}

function save(name, value) {
    try {
        upsertDocument.run(name, JSON.stringify(value), Date.now());
    } catch (error) {
        console.error(`[freeride] Could not save ${name}: ${error.message}`);
    }
}

module.exports = {loadPlayers, savePlayers, load, save, DATA_DIR, FILE};
