import { defineConfig } from '@playwright/test';

export default defineConfig({
    testDir: './tests',
    testMatch: '*.spec.ts',
    use: {
        baseURL: 'http://127.0.0.1:4178',
        viewport: { width: 1280, height: 800 },
        launchOptions: {
            executablePath: process.env.LHRP_CHROMIUM_PATH,
            args: ['--no-sandbox'],
        },
    },
    webServer: {
        command: 'npm run dev -- --port 4178 --strictPort',
        url: 'http://127.0.0.1:4178',
        reuseExistingServer: !process.env.CI,
    },
});
