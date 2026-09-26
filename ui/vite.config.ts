import preact from '@preact/preset-vite';
import stylex from '@stylexjs/unplugin';
import { defineConfig } from 'vite';

// Built into the project's resources; the client build copies it next to the
// DLL as bin/ui, which the CEF view serves from fw://mafia1online/.
export default defineConfig({
    plugins: [stylex.vite(), preact()],
    base: './',
    build: {
        outDir: '../resources/ui',
        emptyOutDir: true,
        sourcemap: false,
        // The embedded Chromium is pinned by the framework (CEF 150), but keep
        // the output conservative so a CEF downgrade does not break the menu.
        target: 'chrome110',
        assetsInlineLimit: 0,
        rollupOptions: {
            // Stable names: the client build overwrites bin/ui in place, so
            // hashed names would pile up there.
            output: {
                entryFileNames: 'app.js',
                chunkFileNames: 'chunks/[name].js',
                assetFileNames: 'assets/[name][extname]',
            },
        },
    },
});
