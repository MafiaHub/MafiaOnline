// Builds the Free Ride resource into resources/freeride: one server bundle,
// one client bundle, the HUD pages and the resource's package.json. The output
// is committed (like resources/ui), so the server and tools/release.py ship it
// without a Node toolchain. Not minified: server owners can still read it.
import { copyFile, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { build } from 'esbuild';

const root = fileURLToPath(new URL('..', import.meta.url));
const output = resolve(root, '../../resources/freeride');
const banner =
    '// Built from gamemodes/freeride by scripts/build.mjs. Do not edit: change the source and rebuild.';
const meta = JSON.parse(await readFile(resolve(root, 'src/resource.json'), 'utf8'));
const check = process.argv.includes('--check');
const target = check ? resolve(root, 'node_modules/.build-check') : output;

await rm(target, { recursive: true, force: true });
await mkdir(target, { recursive: true });
const common = {
    bundle: true,
    charset: 'utf8',
    legalComments: 'none',
    banner: { js: banner },
    logLevel: 'warning',
};
await build({
    ...common,
    entryPoints: [resolve(root, 'src/server/index.ts')],
    outfile: resolve(target, 'server.js'),
    platform: 'node',
    target: 'node22',
    format: 'cjs',
});
await build({
    ...common,
    entryPoints: [resolve(root, 'src/client/index.ts')],
    outfile: resolve(target, 'client.js'),
    platform: 'neutral',
    target: 'es2022',
    format: 'iife',
});
for (const page of meta.pages) {
    await mkdir(dirname(resolve(target, page)), { recursive: true });
    await copyFile(resolve(root, 'src', page), resolve(target, page));
}
const { pages, ...info } = meta;
await writeFile(
    resolve(target, 'package.json'),
    JSON.stringify(
        {
            ...info,
            mafiahub: { serverScripts: ['server.js'], clientScripts: ['client.js'], files: pages },
        },
        null,
        2,
    ) + '\n',
);

if (check) {
    // The committed output must be what the source builds to.
    const stale = [];
    for (const name of ['server.js', 'client.js', 'package.json', ...pages]) {
        const [built, committed] = await Promise.all([
            readFile(resolve(target, name), 'utf8'),
            readFile(resolve(output, name), 'utf8').catch(() => null),
        ]);
        if (built !== committed?.replaceAll('\r\n', '\n')) stale.push(name);
    }
    await rm(target, { recursive: true, force: true });
    if (stale.length) {
        console.error(
            `resources/freeride is out of date (${stale.join(', ')}): run npm run build.`,
        );
        process.exit(1);
    }
    console.log('resources/freeride matches the source.');
} else {
    console.log(`Built ${output}`);
}
