// Entering the city. A joining player logs in or registers (lib/accounts.js)
// while a camera flies over the map (lib/flyover.js). Logging in attaches
// the account's player record and puts them straight on the street: where
// they left off, or at their usual district. Only after the very first
// registration do they pick a look (the stock characters below); later looks
// come from play and are kept in the record's `model` field.
//
// Each map's districts are listed in lib/maps.js. Most are the mission's own
// emeth_* frames: the client reports them and the server checks each against
// that map's stock position, so no client can choose its own spawn point.
// Districts without a frame in the loaded scene (Lost Heaven Extended's
// countryside) are placed by the server alone. Each waiting player is held
// in a private virtual world so another player's ped never walks into their
// preview. The server keeps a returning player's last street position,
// health, weapons and wanted level in their record.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const maps = require("./lib/maps.js");
const store = require("./lib/store.js");
const flyover = require("./lib/flyover.js");
const {createAccounts} = require("./lib/accounts.js");
const commands = require("./lib/commands.js");
const {toast} = require("./lib/notify.js");
const {MAIN_WORLD, inFreeRide, distance, plain, headingOf, emit, onClient, onStart, onStop} = require("./lib/util.js");

// Stock models from a2.dta; each client also drops any that fail the full
// native skeleton check before offering them. Left out on purpose: the
// undressed TommyNAHAC and the shirtless boxers (Boxer01-04, TommyBOXER);
// uniforms (police, firemen, guards, lifeguards, railwaymen), which jobs
// hand out; cutscene variants (HIGH, BLOOD, corpse); and joke models.
const CHARACTERS = [
    {name: "Tommy Angelo", file: "Tommy.i3d", role: "The Driver"},
    {name: "Tommy, Hat On", file: "TommyHAT.i3d", role: "The Driver"},
    {name: "Tommy, Coat and Hat", file: "TommyCOATHAT.i3d", role: "The Made Man"},
    {name: "Tommy the Cabbie", file: "TommyTAXIDRIVER.i3d", role: "The Cabbie"},
    {name: "Tommy the Sailor", file: "TommySAILOR.i3d", role: "The Sailor"},
    {name: "Tommy the Racer", file: "TommyRACER.i3d", role: "The Racer"},
    {name: "Paulie", file: "Paulie.i3d", role: "The Hothead"},
    {name: "Paulie, Coat and Hat", file: "PaulieCOATHAT.i3d", role: "The Hothead"},
    {name: "Sam", file: "Sam.i3d", role: "The Muscle"},
    {name: "Sam, Coat and Hat", file: "SamCOATHAT.i3d", role: "The Muscle"},
    {name: "Frank Colletti", file: "frank.i3d", role: "The Consigliere"},
    {name: "Vincenzo", file: "VincenzoLOW.i3d", role: "The Armourer"},
    {name: "Don Salieri", file: "Salieri2.i3d", role: "The Don"},
    {name: "Don Morello", file: "MorelloLOW.i3d", role: "The Rival Don"},
    {name: "Sergio Morello", file: "Sergio.i3d", role: "The Enforcer"},
    {name: "Luigi", file: "Luigi.i3d", role: "The Barkeep"},
    {name: "Lucas Bertone", file: "Lucas.i3d", role: "The Car Man"},
    {name: "Ralph", file: "Ralph.i3d", role: "The Mechanic"},
    {name: "Pepe", file: "Pepe.i3d", role: "The Fixer"},
    {name: "Carlo", file: "Carlo.i3d", role: "The Hired Gun"},
    {name: "Joe", file: "Joe.i3d", role: "The Regular"},
    {name: "Tony", file: "Tony.i3d", role: "The Kid"},
    {name: "Biff", file: "Biff.i3d", role: "The Loudmouth"},
    {name: "Miller", file: "Miller.i3d", role: "The Businessman"},
    {name: "Michelle", file: "Michelle.i3d", role: "The Informant"},
    {name: "Sarah", file: "Sarah1.i3d", role: "The Bartender's Daughter"},
    {name: "Sarah, Evening Dress", file: "Sarah2.i3d", role: "The Bartender's Daughter"},
    {name: "Berta", file: "Berta.i3d", role: "The Landlady"},
    {name: "Alice", file: "Alice1.i3d", role: "The Socialite"},
    {name: "Morello Soldier", file: "Gangster01.i3d", role: "The Rival"},
    {name: "Morello Gunman", file: "Gangster02.i3d", role: "The Rival"},
    {name: "Morello Heavy", file: "Gangster03.i3d", role: "The Rival"},
    {name: "Morello Driver", file: "Gangster04.i3d", role: "The Rival"},
    {name: "Morello Capo", file: "Gangster05.i3d", role: "The Rival"},
    {name: "Salieri's Man", file: "SalMan03.i3d", role: "The Family"},
    {name: "Salieri's Lookout", file: "SalMan04.i3d", role: "The Family"},
    {name: "Salieri's Driver", file: "SalMan05.i3d", role: "The Family"},
    {name: "Street Tough", file: "Hoolig02.i3d", role: "The Outsider"},
    {name: "Street Punk", file: "Hoolig01.i3d", role: "The Outsider"},
    {name: "Dock Rat", file: "Hoolig03.i3d", role: "The Outsider"},
    {name: "Alley Cat", file: "Hoolig04.i3d", role: "The Outsider"},
    {name: "Corner Boy", file: "Hoolig05.i3d", role: "The Outsider"},
    {name: "Brawler", file: "Hoolig06.i3d", role: "The Outsider"},
    {name: "Free Rider", file: "FREEgang01.i3d", role: "The Drifter"},
    {name: "Road Gang", file: "FREEgang02.i3d", role: "The Drifter"},
    {name: "Private Eye", file: "Detektiv01.i3d", role: "The Detective"},
    {name: "Gumshoe", file: "Detektiv02.i3d", role: "The Detective"},
    {name: "Bookmaker", file: "Bookmaker01.i3d", role: "The Bookie"},
    {name: "Barman", file: "Barman01.i3d", role: "The Barkeep"},
    {name: "Politician", file: "Politik.i3d", role: "The Councillor"},
    {name: "Prosecutor", file: "Prokur.i3d", role: "The Prosecutor"},
    {name: "Gentleman", file: "HighCivil.i3d", role: "The Upper Crust"},
    {name: "Dock Worker", file: "Delnik01.i3d", role: "The Worker"},
    {name: "Labourer", file: "Delnik02.i3d", role: "The Worker"},
    {name: "Foreman", file: "Delnik03.i3d", role: "The Worker"},
    {name: "Lady About Town", file: "Czena01.i3d", role: "The Citizen"},
    {name: "Housewife", file: "Czena02.i3d", role: "The Citizen"},
    {name: "Shopgirl", file: "Czena06.i3d", role: "The Citizen"},
    {name: "Widow", file: "Czena07.i3d", role: "The Citizen"},
    {name: "Seamstress", file: "Czena08.i3d", role: "The Citizen"},
    {name: "Drifter", file: "Homeless01.i3d", role: "The Down-and-Out"},
];

const accounts = createAccounts({load: () => store.load("accounts"), save: (doc) => store.save("accounts", doc)});
// player id -> {account, needsLook}
const sessions = new Map();
// player id -> a ticket for the one login request in flight
const inFlight = new Map();
const lastRequest = new Map();
const selections = new Map();
// player id -> the nickname they joined with, which only fills in the form
const joinNames = new Map();
// player id -> {generation, at} of the last login card sent
const opened = new Map();
// The card carries the flight (about 50 KB); a client that has not said it
// got one is sent another after this long.
const OPEN_RESEND_MS = 4000;
const reportedReady = new Set();
const previewWorlds = new Map();
const FIRST_PREVIEW_WORLD = 0x40000000;
let nextPreviewWorld = FIRST_PREVIEW_WORLD;
const MAX_ANCHOR_DELTA = 3;
const REQUEST_INTERVAL_MS = 1000;
// Logins survive a restart of this resource for this long.
const HANDOFF_MS = 10 * 60_000;
// Often enough that a dropped connection loses only a few steps. Not on
// disconnect: life.js has already ended the player's activity by then.
const RESUME_SAVE_MS = 5000;
let resumeTimer = null;

function connected(player) {
    return World.getPlayers().some((other) => other.id === player.id);
}

function isolatePreview(player) {
    if (!inFreeRide() || player.spawned) return;
    let world = previewWorlds.get(player.id);
    if (world === undefined) {
        world = nextPreviewWorld++;
        previewWorlds.set(player.id, world);
    }
    if (player.virtualWorld !== world) player.setVirtualWorld(world);
}

function isolateWaitingPlayers() {
    for (const player of World.getPlayers()) isolatePreview(player);
}

function leavePreviewWorlds() {
    for (const player of World.getPlayers()) {
        if (previewWorlds.has(player.id)) player.setVirtualWorld(MAIN_WORLD);
    }
    previewWorlds.clear();
}

function point(value) {
    if (!value || ![value.x, value.y, value.z].every(Number.isFinite)) return null;
    return {x: value.x, y: value.y, z: value.z};
}

// The client supplies the facing; the position must match the stock frame.
function validAnchor(report, frame) {
    const stock = maps.current()?.frames[frame];
    if (!stock || !report || report.frame !== frame) return null;
    const position = point(report.position);
    const direction = point(report.direction);
    if (!position || !direction || Math.hypot(direction.x, direction.z) < 0.25 || distance(position, stock) > MAX_ANCHOR_DELTA) return null;
    return {position: stock, direction};
}

function anchorHeading(anchor) {
    return Math.atan2(anchor.direction.x, anchor.direction.z);
}

// The last place a player stood on this map's streets, saved while they
// play. A race, a duel, a job or a cell is not a place to come back to, so
// the previous spot is kept through them.
function saveResume(player) {
    if (!inFreeRide() || !player.spawned || !player.alive || player.missionGeneration !== World.getMissionGeneration() ||
        player.virtualWorld !== MAIN_WORLD || life.activity(player)) return;
    const position = plain(player.position);
    if (![position.x, position.y, position.z].every(Number.isFinite)) return;
    const {weapons, selected} = life.loadoutOf(player);
    profiles.setField(player, "resume", {mission: World.getMission(), position, heading: headingOf(player.rotation), health: player.health,
        weapons, selected, wanted: Police.getRecord(player)?.level ?? 0, at: Date.now()});
}

function resumeOf(player) {
    const record = profiles.get(player);
    const resume = record?.resume;
    if (!resume || resume.mission !== World.getMission() || (record.jailUntil ?? 0) > Date.now()) return null;
    return resume;
}

function nearestDistrict(selection, position) {
    let best = null;
    for (const district of maps.current()?.districts ?? []) {
        const anchor = selection.anchors.get(district.id);
        if (anchor && (!best || distance(anchor.position, position) < distance(best.anchor.position, position))) best = {district, anchor};
    }
    return best?.district ?? null;
}

// The player's usual district on this map, else the map's first one.
function homeDistrict(player, selection) {
    const districts = maps.current()?.districts ?? [];
    const usual = profiles.get(player)?.district;
    return districts.find((district) => district.id === usual && selection.anchors.has(district.id)) ??
        districts.find((district) => selection.anchors.has(district.id)) ?? null;
}

function currentSelection(player) {
    const selection = selections.get(player.id);
    return selection && selection.generation === World.getMissionGeneration() ? selection : null;
}

// ---- entering the city -------------------------------------------------

function enter(player) {
    const session = sessions.get(player.id);
    const selection = currentSelection(player);
    if (!inFreeRide() || !World.isReady() || player.spawned || !session || session.needsLook || !selection) return false;
    const record = profiles.get(player);
    const resume = resumeOf(player);
    const home = homeDistrict(player, selection);
    const anchor = resume ? {position: resume.position, direction: {x: Math.sin(resume.heading), y: 0, z: Math.cos(resume.heading)}}
        : home && selection.anchors.get(home.id);
    if (!anchor || !record) return false;

    // The saved look; a record whose file the server rejects falls back to
    // the first stock character.
    if (!record.model || !player.setModel(record.model)) {
        record.model = CHARACTERS[0].file;
        player.setModel(record.model);
        profiles.setField(player, "model", record.model);
    }
    if (!player.spawn(anchor.position, anchorHeading(anchor))) {
        emit(player, "fr:login:error", {message: "That entrance is unavailable. Try again in a moment."});
        return false;
    }
    player.setVirtualWorld(MAIN_WORLD);
    previewWorlds.delete(player.id);
    // Dead players come back at their usual district, not the resume spot.
    selection.choice = home ? {locationId: home.id} : null;
    if (home && !record.district) profiles.setField(player, "district", home.id);
    if (resume) {
        // The spawn handlers gave the starting kit; this life carries on the last one.
        life.equip(player, resume.weapons ?? [], resume.selected);
        if (resume.health > 0 && resume.health < 100) player.setHealth(resume.health);
        if (resume.wanted > 0) Police.setWanted(player, resume.wanted);
    }
    const near = resume && nearestDistrict(selection, resume.position);
    const where = resume ? `where they left off${near ? ` near ${near.name}` : ""}` : home.name;
    emit(player, "fr:login:accepted", {where});
    console.log(`[freeride] ${player.nickname} entered ${where}`);
    return true;
}

// The district the one-time look is chosen at, where the player will start.
function lookAnchor(player) {
    const selection = currentSelection(player);
    const home = selection && homeDistrict(player, selection);
    const anchor = home && selection.anchors.get(home.id);
    return anchor ? {position: anchor.position, direction: anchor.direction} : null;
}

// Until they log in, players are "Guest <id>" (accounts.js reserves the
// prefix), so nobody can chat or be listed under an account's name.
function makeGuest(player) {
    const guest = `Guest ${player.id}`;
    if (!sessions.has(player.id) && player.nickname !== guest) player.setNickname(guest);
}

// After a password, a token or a fresh registration.
function inTown(player, account) {
    return World.getPlayers().some((candidate) => candidate.id !== player.id && sessions.get(candidate.id)?.account === account.name.toLowerCase());
}

const IN_USE = {ok: false, field: "form", code: "in-use", message: "That account is already in town."};

function signIn(player, account, extra = {}) {
    if (inTown(player, account)) return IN_USE;
    // A guest who joined under this name gives it up to its owner.
    const holder = World.getPlayers().find((candidate) => candidate.id !== player.id && candidate.nickname.toLowerCase() === account.name.toLowerCase());
    if (holder) holder.setNickname(`Guest ${holder.id}`);
    player.setNickname(account.name);

    const {record, returning} = profiles.attach(player, account.key);
    const needsLook = !record.model;
    sessions.set(player.id, {account: account.name.toLowerCase(), needsLook});
    Events.emitLocal("fr:login", player, {returning, account: account.name, claimed: Boolean(extra.claimed)});
    console.log(`[freeride] ${player.nickname} logged in${extra.registered ? " (new account" + (extra.claimed ? ", claimed an earlier record)" : ")") : ""}`);
    return {ok: true, name: player.nickname, needsLook, ...(needsLook ? {characters: CHARACTERS, anchor: lookAnchor(player)} : {})};
}

function finish(player, result) {
    emit(player, "fr:login:result", result);
    if (result.ok && !result.needsLook) enter(player);
}

// One request at a time per player, at most one a second; the answer is
// dropped if the player left or another request took its place.
async function guarded(player, work) {
    const now = Date.now();
    if (inFlight.has(player.id)) return null;
    if (now - (lastRequest.get(player.id) ?? 0) < REQUEST_INTERVAL_MS) return {ok: false, field: "form", message: "Not so fast. Try again in a second."};
    lastRequest.set(player.id, now);
    const ticket = {};
    inFlight.set(player.id, ticket);
    let result;
    try {
        result = await work();
    } catch (error) {
        console.error(`[freeride] Login for ${player.nickname} failed: ${error?.stack ?? error}`);
        result = {ok: false, field: "form", message: "The city could not check that. Try again."};
    } finally {
        if (inFlight.get(player.id) === ticket) inFlight.delete(player.id);
    }
    if (connected(player) && !sessions.has(player.id)) return result;
    return null;
}

function canLogIn(player, request) {
    return inFreeRide() && request && !sessions.has(player.id) && !player.spawned;
}

onClient("fr:login:submit", async (player, request) => {
    if (!canLogIn(player, request) || typeof request.name !== "string" || typeof request.password !== "string") return;
    const address = player.ip;
    const result = await guarded(player, async () => {
        if (request.mode === "register") {
            // Only a record from this same machine; see lib/accounts.js.
            const claim = player.hardwareId
                ? {key: profiles.legacyKey(request.name.trim().replace(/\s+/g, " "), player.hardwareId), exists: profiles.exists} : null;
            return accounts.register({name: request.name, password: request.password, address, claim});
        }
        return accounts.login({name: request.name, password: request.password, address, logoutOthers: request.logoutOthers === true});
    });
    if (!result) return;
    if (!result.ok) {
        finish(player, result);
        return;
    }
    const signed = signIn(player, result.account, {registered: request.mode === "register", claimed: result.claimed});
    if (signed.ok && request.remember) Object.assign(signed, accounts.issueToken(result.account, player.hardwareId));
    finish(player, signed);
});

onClient("fr:login:token", async (player, request) => {
    if (!canLogIn(player, request) || typeof request.token !== "string") return;
    // An account already in town leaves the token unspent for later.
    const result = await guarded(player, async () => {
        const owner = accounts.ownerOf(request.token);
        return owner && inTown(player, owner) ? IN_USE : accounts.useToken({token: request.token, address: player.ip, hardwareId: player.hardwareId});
    });
    if (!result) return;
    if (!result.ok) {
        finish(player, result);
        return;
    }
    const signed = signIn(player, result.account);
    if (!signed.ok) accounts.revokeToken(result.token);
    finish(player, signed.ok ? {...signed, token: result.token, expires: result.expires} : signed);
});

onClient("fr:login:forget", (player, request) => {
    if (typeof request?.token === "string") accounts.revokeToken(request.token);
});

onClient("fr:login:look", (player, request) => {
    const session = sessions.get(player.id);
    const character = CHARACTERS.find((entry) => entry.file === request?.model);
    if (!session?.needsLook || !character || player.spawned || request.generation !== World.getMissionGeneration()) return;
    profiles.setField(player, "model", character.file);
    session.needsLook = false;
    if (!enter(player)) {
        session.needsLook = true;
        profiles.setField(player, "model", null);
        emit(player, "fr:login:error", {message: "That entrance is unavailable. Try again in a moment."});
    }
});

// ---- the hello ---------------------------------------------------------

onClient("fr:login:hello", (player, report) => {
    isolatePreview(player);
    if (!reportedReady.has(player.id)) {
        reportedReady.add(player.id);
        console.log(`[freeride] Login report from ${player.nickname}: frames ${report?.anchors?.map((anchor) => anchor?.frame).join(",")}`);
    }
    if (!inFreeRide() || !World.isReady() || player.spawned ||
        report?.generation !== World.getMissionGeneration() || !Array.isArray(report.anchors)) return;

    const anchors = new Map();
    for (const district of maps.current().districts) {
        const anchor = district.frame
            ? validAnchor(report.anchors.find((candidate) => candidate?.frame === district.frame), district.frame)
            : {position: district.position, direction: district.direction};
        if (anchor) anchors.set(district.id, anchor);
    }
    if (anchors.size < 2) return;

    const previous = selections.get(player.id);
    const fresh = !previous || previous.generation !== report.generation;
    const selection = fresh ? {generation: report.generation, anchors, choice: null} : previous;
    selection.anchors = anchors;
    selections.set(player.id, selection);
    const session = sessions.get(player.id);
    if (!session) makeGuest(player);
    // Once per map, and again only if the client still has none after a
    // while (it started over, or the card was lost).
    const last = opened.get(player.id);
    const due = !last || last.generation !== report.generation || (report.opened !== true && Date.now() - last.at > OPEN_RESEND_MS);
    if (due) {
        opened.set(player.id, {generation: report.generation, at: Date.now()});
        emit(player, "fr:login:open", {generation: report.generation, mission: World.getMission(), serverId: accounts.serverId,
            nickname: joinNames.get(player.id) ?? "", authenticated: session ? player.nickname : null, flight: flyover.flightFor(maps.current().id),
            fallbackModel: CHARACTERS[0].file});
        // Logged in already (a new map): the next step follows straight away.
        if (session?.needsLook) emit(player, "fr:login:result", {ok: true, name: player.nickname, needsLook: true, characters: CHARACTERS, anchor: lookAnchor(player)});
    }
    if (session && !session.needsLook) enter(player);
});

// ---- lifecycle ---------------------------------------------------------

Events.on("playerConnect", (player) => {
    joinNames.set(player.id, player.nickname);
    if (inFreeRide()) makeGuest(player);
    isolatePreview(player);
});
Events.on("playerMissionReady", isolatePreview);
Events.on("missionChange", () => {
    selections.clear();
    if (inFreeRide()) isolateWaitingPlayers();
    else leavePreviewWorlds();
});

// A restart of this resource keeps the players connected and in the city;
// their logins are handed over through the database.
function restoreSessions() {
    const handoff = store.load("login_sessions");
    store.save("login_sessions", {at: 0, sessions: []});
    if (!handoff || Date.now() - handoff.at > HANDOFF_MS) return;
    for (const saved of handoff.sessions ?? []) {
        const player = World.getPlayers().find((candidate) => candidate.id === saved.id && candidate.hardwareId === saved.hardwareId &&
            candidate.nickname === saved.nickname);
        const account = player && accounts.byName(saved.account);
        if (!account || sessions.has(player.id)) continue;
        const {record, returning} = profiles.attach(player, account.key);
        sessions.set(player.id, {account: account.name.toLowerCase(), needsLook: !record.model && !player.spawned});
        Events.emitLocal("fr:login", player, {returning, account: account.name, restored: true});
    }
}

// Whoever the handoff did not cover goes back to the login card.
function returnToLogin() {
    if (!inFreeRide()) return;
    for (const player of World.getPlayers()) {
        if (sessions.has(player.id)) continue;
        makeGuest(player);
        if (player.spawned) player.despawn();
    }
}

onStart(() => {
    restoreSessions();
    returnToLogin();
    isolateWaitingPlayers();
    resumeTimer = setInterval(() => World.getPlayers().forEach(saveResume), RESUME_SAVE_MS);
});
onStop(() => {
    clearInterval(resumeTimer);
    for (const player of World.getPlayers()) saveResume(player);
    store.save("login_sessions", {at: Date.now(), sessions: World.getPlayers().filter((player) => sessions.has(player.id))
        .map((player) => ({id: player.id, nickname: player.nickname, hardwareId: player.hardwareId, account: sessions.get(player.id).account}))});
    selections.clear();
    leavePreviewWorlds();
});

// Dead players come back at their usual district; without one (another
// mission, or after a resource restart) life.js falls back to the native
// spawn hint.
life.setRespawnProvider((player) => {
    if (!inFreeRide()) return null;
    const selection = currentSelection(player);
    const anchor = selection?.choice ? selection.anchors.get(selection.choice.locationId) : null;
    return anchor ? {position: anchor.position, heading: anchorHeading(anchor)} : null;
});

Events.on("playerDisconnect", (player) => {
    sessions.delete(player.id);
    joinNames.delete(player.id);
    opened.delete(player.id);
    inFlight.delete(player.id);
    lastRequest.delete(player.id);
    selections.delete(player.id);
    reportedReady.delete(player.id);
    previewWorlds.delete(player.id);
});

commands.register({name: "logout", usage: "/logout", help: "Sign out every computer that remembers your login.", category: "City"}, (player) => {
    const session = sessions.get(player.id);
    if (!session) return;
    const count = accounts.revokeAll(session.account);
    toast(player, count ? `Signed out ${count} remembered login${count === 1 ? "" : "s"}. Next time you log in with your password.`
        : "No computer remembers your login.", {title: "Account", tone: "good"});
});

// ---- console -----------------------------------------------------------

Events.on("consoleCommand", async (command, args) => {
    if (command !== "account") return;
    const [action, name, ...rest] = args;
    if (action === "list") {
        for (const entry of accounts.list()) {
            console.log(`[freeride] ${entry.name}: record ${entry.key}, ${entry.tokens} remembered, last login ${new Date(entry.lastLogin).toISOString()}`);
        }
    } else if (action === "revoke" && name) {
        console.log(`[freeride] ${accounts.revokeAll(name)} remembered logins of ${name} revoked`);
    } else if (action === "password" && name && rest.length) {
        const result = await accounts.setPassword(name, rest.join(" "));
        console.log(`[freeride] ${result.ok ? `Password of ${name} changed; its remembered logins were revoked` : result.message}`);
    } else if (action === "claim" && name && rest[0]) {
        const online = World.getPlayers().some((player) => sessions.get(player.id)?.account === name.toLowerCase());
        const result = online ? {ok: false, message: `${name} is in town; try again once they have left.`}
            : profiles.exists(rest[0]) ? accounts.assignRecord(name, rest[0]) : {ok: false, message: `No player record ${rest[0]}.`};
        console.log(`[freeride] ${result.ok ? `${result.account.name} now owns record ${rest[0]}` : result.message}`);
    } else {
        console.log("[freeride] Console: account list | account revoke <name> | account password <name> <new password> | account claim <name> <record key>");
    }
});
