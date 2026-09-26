import { copyFile, mkdir, readdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { build } from 'esbuild';
import { build as buildUi } from 'vite';

const root = fileURLToPath(new URL('..', import.meta.url));
const runtime = resolve(root, '../../build/real-life');
const resource = resolve(runtime, 'resources/lost-heaven-roleplay');
const mods = resolve(runtime, 'mods');

await mkdir(mods, { recursive: true });
await copyFile(
    resolve(root, '../../mods/freeride-extended.zip'),
    resolve(mods, 'freeride-extended.zip'),
);
await mkdir(resource, { recursive: true });
await build({
    entryPoints: [resolve(root, 'src/server/main.ts')],
    outfile: resolve(resource, 'server.cjs'),
    bundle: true,
    platform: 'node',
    target: 'node25',
    format: 'cjs',
});
await build({
    entryPoints: [resolve(root, 'src/client/main.ts')],
    outfile: resolve(resource, 'client.js'),
    bundle: true,
    platform: 'neutral',
    target: 'es2022',
    format: 'iife',
});
await buildUi({ root, configFile: resolve(root, 'vite.config.ts') });
const assets = (await readdir(resolve(resource, 'ui'), { recursive: true, withFileTypes: true }))
    .filter((entry) => entry.isFile())
    .map((entry) =>
        resolve(entry.parentPath, entry.name)
            .slice(resource.length + 1)
            .replaceAll('\\', '/'),
    );
await writeFile(
    resolve(resource, 'package.json'),
    JSON.stringify(
        {
            name: 'lost-heaven-roleplay',
            version: '0.1.0',
            mafiahub: {
                serverScripts: ['server.cjs'],
                clientScripts: ['client.js'],
                files: assets.sort(),
            },
        },
        null,
        2,
    ) + '\n',
);
await copyFile(resolve(root, 'server.example.json'), resolve(runtime, 'server.example.json'));
console.log(
    `Ready: ${runtime}\nRun Mafia1OnlineServer from this folder with --config server.example.json.`,
);
