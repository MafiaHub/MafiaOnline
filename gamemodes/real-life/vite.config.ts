import preact from '@preact/preset-vite';
import stylex from '@stylexjs/unplugin';
import { defineConfig } from 'vite';

export default defineConfig({
    plugins: [stylex.vite(), preact()],
    base: './',
    build: {
        outDir: '../../build/real-life/resources/lost-heaven-roleplay/ui',
        emptyOutDir: true,
        target: 'chrome110',
        assetsInlineLimit: 0,
        sourcemap: false,
    },
});
