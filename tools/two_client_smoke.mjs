#!/usr/bin/env node
// Repeatable local multiplayer smoke test. All artifacts remain in the repo.
import {spawn, spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {createReadStream} from 'node:fs';
import {mkdir, readFile, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const toolsDir = path.join(root, 'code/projects/mafia1online/tools');
const options = new Set(process.argv.slice(2));
const attach = options.has('--attach');
const impair = options.has('--impair');
const lateJoin = !options.has('--no-late-join');
const keepClients = options.has('--keep-clients');
const allowServerMismatch = options.has('--allow-server-mismatch');
const workspace = process.env.MAFIA1ONLINE_TEST_WORKSPACE ?? '8';
if (!/^\d+$/.test(workspace)) throw new Error('MAFIA1ONLINE_TEST_WORKSPACE must be a workspace number');
const out = path.join(root, 'builds/research', `smoke-${new Date().toISOString().replaceAll(':', '-')}`);
const checks = [];
const startedAt = new Date().toISOString();
let spawnedCarId;
let proxy;

if (options.has('--help') || [...options].some(value => !['--attach', '--impair', '--no-late-join', '--keep-clients', '--allow-server-mismatch', '--help'].includes(value))) {
    console.log('Usage: two_client_smoke.mjs [--attach] [--impair] [--no-late-join] [--keep-clients] [--allow-server-mismatch]');
    process.exit(options.has('--help') ? 0 : 2);
}
if (attach && impair) throw new Error('--attach and --impair cannot be combined');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
function sha256(file) {
    return new Promise((resolve, reject) => {
        const hash = createHash('sha256');
        const stream = createReadStream(file);
        stream.on('data', chunk => hash.update(chunk));
        stream.on('error', reject);
        stream.on('end', () => resolve(hash.digest('hex')));
    });
}
function command(exe, args, env = process.env) {
    const result = spawnSync(exe, args, {cwd: root, env, encoding: 'utf8'});
    if (result.error || result.status !== 0) throw new Error(`${exe} ${args.join(' ')}: ${result.error ?? result.stderr ?? result.stdout}`);
    return result.stdout.trim();
}
function clients() {
    return JSON.parse(command('hyprctl', ['clients', '-j']))
        .filter(client => client.class === 'mafia1onlinelauncher.exe')
        .sort((a, b) => a.at[0] - b.at[0]);
}
function focus(client) {
    if (!/^0x[0-9a-f]+$/i.test(client.address)) throw new Error('Invalid window address');
    command('hyprctl', ['eval', `hl.dispatch(hl.dsp.focus({monitor="DP-1"})); hl.dispatch(hl.dsp.focus({workspace="${workspace}"})); hl.dispatch(hl.dsp.focus({window="address:${client.address}"}))`]);
}
async function holdKey(key, ms) {
    command('xdotool', ['keydown', key]);
    try { await sleep(ms); } finally { command('xdotool', ['keyup', key]); }
}
async function shoot(client) {
    focus(client);
    command('xdotool', ['mousemove', '440', '390']);
    // Wine may consume the first press while reacquiring native mouse input.
    await sleep(250);
    command('xdotool', ['mousedown', '1']);
    await sleep(150);
    command('xdotool', ['mouseup', '1']);
    await sleep(250);
    command('xdotool', ['mousedown', '1']);
    try { await sleep(1600); } finally { command('xdotool', ['mouseup', '1']); }
}
async function chat(client, line) {
    focus(client);
    await sleep(250);
    command('xdotool', ['key', 't']);
    await sleep(180);
    command('xdotool', ['type', '--clearmodifiers', '--delay', '80', line]);
    command('xdotool', ['key', 'Return']);
}
async function inspector(expression) {
    const controller = new AbortController();
    const fetchTimer = setTimeout(() => controller.abort(), 3000);
    let target;
    try {
        const response = await fetch('http://127.0.0.1:9229/json/list', {signal: controller.signal});
        target = (await response.json())[0];
    } finally { clearTimeout(fetchTimer); }
    if (!target?.webSocketDebuggerUrl) throw new Error('Server Node inspector is unavailable');
    return await new Promise((resolve, reject) => {
        const ws = new WebSocket(target.webSocketDebuggerUrl);
        const timer = setTimeout(() => { ws.close(); reject(new Error('Server inspector timed out')); }, 8000);
        function done(error, value) {
            clearTimeout(timer);
            ws.close();
            error ? reject(error) : resolve(value);
        }
        ws.addEventListener('open', () => ws.send(JSON.stringify({id: 1, method: 'Runtime.evaluate', params: {expression, returnByValue: true}})));
        ws.addEventListener('message', event => {
            const packet = JSON.parse(event.data);
            if (packet.id !== 1) return;
            if (packet.error || packet.result?.exceptionDetails) done(new Error(JSON.stringify(packet.error ?? packet.result.exceptionDetails)));
            else done(null, packet.result?.result?.value);
        });
        ws.addEventListener('error', () => done(new Error('Server inspector WebSocket failed')));
    });
}
async function state() {
    const serialized = await inspector(`JSON.stringify({mission: World.getMission(), generation: World.getMissionGeneration(), ready: World.isReady(), players: World.getPlayers().map(p => { const at = p.position; const vehicle = p.getVehicle(); return {id: p.id, state: {nickname: p.nickname, spawned: p.spawned, alive: p.alive, health: p.health, x: at.x, y: at.y, z: at.z, carId: vehicle ? vehicle.id : 0, seat: p.getSeat()}, combat: p.getCombatState()}; }), cars: World.getVehicles().map(v => ({id: v.id, state: {model: v.model, engineOn: v.engineOn, meshRevision: v.meshRevision}, damage: v.getDamageState()}))})`);
    return JSON.parse(serialized);
}
async function until(description, predicate, timeout = 30000) {
    const end = Date.now() + timeout;
    let last;
    while (Date.now() < end) {
        try {
            last = await state();
            if (predicate(last)) return last;
        } catch (error) { last = error.message; }
        await sleep(400);
    }
    const summary = last && typeof last === 'object' ? {
        mission: last.mission,
        players: last.players?.map(entry => ({id: entry.id, nickname: entry.state?.nickname, spawned: entry.state?.spawned, colt: entry.combat?.ammo[6]?.loaded})),
        cars: last.cars?.map(entry => ({id: entry.id, engineOn: entry.state?.engineOn, meshRevision: entry.state?.meshRevision})),
    } : last;
    throw new Error(`${description} timed out: ${JSON.stringify(summary)}`);
}
async function check(name, task) {
    try {
        const result = await task();
        checks.push({name, passed: true, result});
        console.log(`PASS ${name}`);
    } catch (error) {
        checks.push({name, passed: false, error: error.message});
        console.error(`FAIL ${name}: ${error.message}`);
    }
}
function player(snapshot, nickname) {
    return snapshot.players.find(entry => entry.state?.nickname === nickname);
}
function screenshot(name) {
    const windows = clients();
    if (windows.length !== 2) throw new Error(`Expected two test windows for screenshot, found ${windows.length}`);
    const left = Math.min(...windows.map(window => window.at[0]));
    const top = Math.min(...windows.map(window => window.at[1]));
    const right = Math.max(...windows.map(window => window.at[0] + window.size[0]));
    const bottom = Math.max(...windows.map(window => window.at[1] + window.size[1]));
    command('hyprctl', ['eval', `hl.dispatch(hl.dsp.focus({monitor="DP-1"})); hl.dispatch(hl.dsp.focus({workspace="${workspace}"}))`]);
    command('grim', ['-g', `${left},${top} ${right - left}x${bottom - top}`, path.join(out, `${name}.png`)]);
}

async function enterFreeRide(windows) {
    const initial = await until('two connected clients', s => s.ready && s.players.length === 2, 45000);
    if (!['freeride', 'freeridenoc'].includes(initial.mission)) return;
    // The sample opens a server-validated character selector. Press Enter
    // only after the mission is ready; it may need another attempt while the
    // client is still loading its native preview frames.
    for (let attempt = 0; attempt < 12; attempt++) {
        const snapshot = await state();
        if (snapshot.players.every(p => p.state?.spawned)) return;
        for (const window of windows) {
            focus(window);
            command('xdotool', ['key', 'Return']);
            await sleep(350);
        }
        await sleep(1200);
    }
}

await mkdir(out, {recursive: true});
const serverPids = command('pidof', ['Mafia1OnlineServer']).split(/\s+/);
if (serverPids.length !== 1 || !/^\d+$/.test(serverPids[0])) throw new Error('Expected exactly one running Mafia1Online server');
const build = {
    game: await sha256(path.join(root, 'builds/runtime/game/Mafia/Mafia/Game.exe')),
    client: await sha256(path.join(root, 'builds/build-32/bin/Mafia1OnlineClient.dll')),
    serverRunning: await sha256(`/proc/${serverPids[0]}/exe`),
    serverFile: await sha256(path.join(root, 'builds/build-linux-64/bin/Mafia1OnlineServer')),
};
if (build.game !== '303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298') throw new Error('Test Game.exe does not match the supported retail build');
if (!allowServerMismatch && build.serverRunning !== build.serverFile) throw new Error('Running server differs from the latest built server; restart it before release verification');
try {
    if (impair) {
        proxy = spawn('python3', [path.join(toolsDir, 'udp_test_proxy.py'), '--listen-port', '27017', '--server-port', '27015', '--one-way-ms', '50', '--jitter-ms', '10', '--loss-percent', '2'], {cwd: root, stdio: 'ignore'});
        await sleep(300);
        if (proxy.exitCode !== null) throw new Error('UDP impairment proxy exited during startup');
    }
    if (!attach) command(path.join(toolsDir, 'run_two_clients.sh'), [], {...process.env, ...(impair ? {MAFIA1ONLINE_TEST_PORT: '27017'} : {})});
    const windows = clients();
    if (windows.length !== 2) throw new Error(`Expected two test windows, found ${windows.length}`);
    await enterFreeRide(windows);
    const firstConfig = JSON.parse(await readFile(path.join(root, 'builds/runtime/config/client-first.json'), 'utf8'));
    const firstNickname = firstConfig.quickJoin?.nickname;
    if (typeof firstNickname !== 'string' || !firstNickname) throw new Error('First test nickname is missing from the client config');
    await check('two players spawned on one mission', async () => {
        const snapshot = await until('two spawned players', s => s.ready && s.players.length === 2 && s.players.every(p => p.state?.spawned));
        screenshot('spawn');
        return {mission: snapshot.mission, players: snapshot.players.map(p => ({id: p.id, nickname: p.state.nickname, health: p.state.health}))};
    });
    await check('first player movement reaches server', async () => {
        const before = await state();
        const first = player(before, firstNickname);
        if (!first) throw new Error(`Player ${firstNickname} is missing`);
        focus(windows[0]);
        await sleep(250);
        await holdKey('w', 1000);
        await sleep(800);
        const after = await state();
        const moved = player(after, firstNickname);
        if (!moved) throw new Error(`Player ${firstNickname} disconnected during movement`);
        const distance = Math.hypot(moved.state.x - first.state.x, moved.state.y - first.state.y, moved.state.z - first.state.z);
        if (distance < 0.05) throw new Error(`Server position changed only ${distance.toFixed(3)} units`);
        screenshot('movement');
        return {distance};
    });
    await check('native Colt shot consumes server ammo', async () => {
        const before = player(await state(), firstNickname)?.combat;
        if (!before) throw new Error('Player combat state missing');
        await shoot(windows[0]);
        const after = await until('server accepted a shot', s => {
            const combat = player(s, firstNickname)?.combat;
            return combat && combat.ammo[6].loaded < before.ammo[6].loaded;
        }, 5000);
        screenshot('shot');
        return {before: before.ammo[6].loaded, after: player(after, firstNickname).combat.ammo[6].loaded};
    });
    await check('/car creates a replicated vehicle', async () => {
        const previousIds = new Set((await state()).cars.map(car => car.id));
        await chat(windows[0], '/car');
        const after = await until('car spawn', s => s.cars.some(car => !previousIds.has(car.id)), 8000);
        const created = after.cars.find(car => !previousIds.has(car.id));
        spawnedCarId = created.id;
        await sleep(1200);
        screenshot('car');
        return {id: created.id, model: created.state.model};
    });
    await check('/engine rejects a pedestrian', async () => {
        const before = await state();
        const car = before.cars.find(entry => entry.id === spawnedCarId);
        if (!car) throw new Error('No car for /engine');
        await chat(windows[0], '/engine');
        await sleep(900);
        const after = await state();
        const current = after.cars.find(entry => entry.id === car.id);
        if (!current || current.state.engineOn !== car.state.engineOn) throw new Error('Pedestrian changed the engine state');
        screenshot('engine');
        return {id: car.id, engineOn: current.state.engineOn};
    });
    if (lateJoin) {
        await check('late join reconstructs the world', async () => {
            const old = await state();
            const carIds = old.cars.map(car => car.id);
            const oldObserver = player(old, 'Smoke2')?.id;
            if (!oldObserver) throw new Error('Observer player is missing before late join');
            process.kill(windows[1].pid, 'SIGTERM');
            const end = Date.now() + 15000;
            while (clients().length !== 1 && Date.now() < end) await sleep(300);
            if (clients().length !== 1) throw new Error('Second test client did not close');
            await until('old observer disconnected', s => s.players.length === 1 && !player(s, 'Smoke2'), 15000);
            command(path.join(toolsDir, 'run_two_clients.sh'), ['--late-join'], {...process.env, ...(impair ? {MAFIA1ONLINE_TEST_PORT: '27017'} : {})});
            await enterFreeRide(clients());
            const joined = await until('late join', s => s.players.length === 2 && s.players.every(p => p.state?.spawned) && player(s, 'Smoke2')?.id !== oldObserver && carIds.every(id => s.cars.some(car => car.id === id)), 45000);
            await sleep(1500);
            screenshot('late-join');
            return {players: joined.players.map(p => p.state.nickname), carIds};
        });
    }
} finally {
    if (!attach && !keepClients) {
        for (const client of clients()) {
            try { process.kill(client.pid, 'SIGTERM'); } catch (error) {
                if (error.code !== 'ESRCH') throw error;
            }
        }
    }
    if (proxy) proxy.kill('SIGTERM');
    await writeFile(path.join(out, 'result.json'), JSON.stringify({startedAt, mode: impair ? '100ms RTT, 10ms jitter, 2% loss' : 'local', sha256: build, checks}, null, 2) + '\n');
    console.log(`Evidence: ${out}`);
}
if (checks.some(check => !check.passed)) process.exitCode = 1;
