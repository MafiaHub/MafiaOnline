import { expect, test } from '@playwright/test';

const rememberedFixture = {
    serverId: '12345678-1234-1234-1234-123456789abc',
    username: 'Angelo',
    token: 'a'.repeat(64),
    expiresAt: Date.now() + 86_400_000,
};

test('remembered sign-in stores a token after success and automatically reconnects once', async ({
    page,
}) => {
    await page.addInitScript((remembered) => {
        const actions: Record<string, unknown>[] = [];
        const state = (detail: Record<string, unknown>) =>
            window.dispatchEvent(
                new CustomEvent('real-life:state', {
                    detail: {
                        generation: 1,
                        connected: true,
                        pending: false,
                        phase: 'auth',
                        serverId: remembered.serverId,
                        ...detail,
                    },
                }),
            );

        Object.assign(window, {
            authActions: actions,
            callEvent(name: string, payload: string) {
                const value = JSON.parse(payload);

                if (name === 'real-life:ready') {
                    setTimeout(() => state({}), 0);
                }

                if (name === 'real-life:auth') {
                    actions.push(value);

                    if (value.mode === 'login' && value.remember) {
                        setTimeout(
                            () => state({ phase: 'spawning', pending: true, remembered }),
                            0,
                        );
                    }
                }

                if (name === 'real-life:remembered-ready') {
                    Object.assign(window, {
                        storedBeforeSpawn:
                            localStorage.getItem(`lhrp.account.${remembered.serverId}`) !== null,
                    });
                }

                return true;
            },
        });
    }, rememberedFixture);

    await page.goto('/');
    await page.getByLabel('Username', { exact: true }).fill('Angelo');
    await page.getByLabel('Password', { exact: true }).fill('my-long-password');
    await page.getByRole('checkbox', { name: 'Sign me in automatically on this device' }).check();
    expect(
        await page.evaluate(() =>
            Object.keys(localStorage).some((key) => key.startsWith('lhrp.account.')),
        ),
    ).toBe(false);

    await page.getByRole('button', { name: 'Return to the city' }).click();
    await expect(page.getByRole('heading', { name: 'Opening the city…' })).toBeVisible();
    const stored = await page.evaluate(
        (id) => localStorage.getItem(`lhrp.account.${id}`),
        rememberedFixture.serverId,
    );

    expect(JSON.parse(stored!)).toEqual(rememberedFixture);
    expect(stored).not.toContain('password');
    expect(
        await page.evaluate(
            () => (window as unknown as { storedBeforeSpawn: boolean }).storedBeforeSpawn,
        ),
    ).toBe(true);

    await page.reload();
    await expect
        .poll(() =>
            page.evaluate(() => (window as unknown as { authActions: unknown[] }).authActions),
        )
        .toEqual([
            {
                mode: 'remembered',
                username: 'Angelo',
                token: rememberedFixture.token,
                locale: 'en',
            },
        ]);

    await page.evaluate(
        (serverId) =>
            window.dispatchEvent(
                new CustomEvent('real-life:state', {
                    detail: {
                        generation: 1,
                        connected: true,
                        phase: 'auth',
                        pending: false,
                        serverId,
                        error: 'alreadyOnline',
                    },
                }),
            ),
        rememberedFixture.serverId,
    );

    await expect(page.getByRole('alert')).toContainText('already in the city');
    await page.getByRole('button', { name: 'Čeština', exact: true }).click();
    expect(
        await page.evaluate(
            () => (window as unknown as { authActions: unknown[] }).authActions.length,
        ),
    ).toBe(1);

    await page.evaluate(
        (serverId) =>
            window.dispatchEvent(
                new CustomEvent('real-life:state', {
                    detail: {
                        generation: 1,
                        connected: true,
                        phase: 'auth',
                        pending: false,
                        serverId,
                        error: 'rememberedExpired',
                    },
                }),
            ),
        rememberedFixture.serverId,
    );

    await expect(page.getByLabel('Heslo', { exact: true })).toBeVisible();
    expect(
        await page.evaluate(
            (id) => localStorage.getItem(`lhrp.account.${id}`),
            rememberedFixture.serverId,
        ),
    ).toBeNull();
});

for (const scenario of ['other-server', 'expired', 'logout'] as const) {
    test(`automatic sign-in respects ${scenario}`, async ({ page }) => {
        await page.addInitScript(
            ({ account, scenario }) => {
                const serverId =
                    scenario === 'other-server'
                        ? '87654321-1234-1234-1234-123456789abc'
                        : account.serverId;

                localStorage.setItem(
                    `lhrp.account.${account.serverId}`,
                    JSON.stringify({
                        ...account,
                        expiresAt: scenario === 'expired' ? 1 : account.expiresAt,
                    }),
                );

                Object.assign(window, {
                    authCount: 0,
                    callEvent(name: string) {
                        if (name === 'real-life:ready') {
                            setTimeout(
                                () =>
                                    window.dispatchEvent(
                                        new CustomEvent('real-life:state', {
                                            detail: {
                                                generation: 1,
                                                connected: true,
                                                pending: false,
                                                phase: 'auth',
                                                serverId,
                                                forgetRemembered: scenario === 'logout',
                                            },
                                        }),
                                    ),
                                0,
                            );
                        }

                        if (name === 'real-life:auth') {
                            (window as unknown as { authCount: number }).authCount++;
                        }

                        return true;
                    },
                });
            },
            { account: rememberedFixture, scenario },
        );

        await page.goto('/');
        await expect(page.getByRole('button', { name: 'Return to the city' })).toBeEnabled();
        await expect(page.getByLabel('Password', { exact: true })).toBeVisible();
        expect(
            await page.evaluate(() => (window as unknown as { authCount: number }).authCount),
        ).toBe(0);
    });
}

test('English and Czech forms validate accessibly on desktop and small screens', async ({
    page,
}) => {
    await page.goto('/');
    await expect(page.getByRole('heading', { name: 'Welcome back.' })).toBeVisible();
    await expect(page.getByLabel('Username', { exact: true })).toHaveCSS(
        'border-top-style',
        'solid',
    );

    await page.screenshot({ path: '../../build/real-life-login.png' });
    await page.getByRole('button', { name: 'New arrival', exact: true }).click();
    await page.getByRole('button', { name: 'Make a new beginning' }).click();
    await expect(page.getByRole('alert')).toContainText('3–24');
    await page.getByLabel('Username', { exact: true }).fill('Angelo');
    await page.getByLabel('Password', { exact: true }).fill('my-long-password');
    await page.getByLabel('Confirm password', { exact: true }).fill('something-else');
    await page.getByRole('button', { name: 'Make a new beginning' }).click();
    await expect(page.getByRole('alert')).toHaveText('The passwords do not match.');
    await page.getByRole('button', { name: 'Čeština', exact: true }).click();
    await expect(page.locator('html')).toHaveAttribute('lang', 'cs');
    await expect(page.getByRole('alert')).toHaveText('Hesla se neshodují.');
    await expect(page.getByRole('heading')).toHaveText('Začněte svůj příběh.');
    await page.screenshot({ path: '../../build/real-life-register-cs.png' });
    await page.setViewportSize({ width: 360, height: 740 });
    await expect(page.getByRole('button', { name: 'Začít nový příběh' })).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(
        true,
    );

    await page.reload();
    await expect(page.getByRole('heading')).toHaveText('Vítejte zpátky.');
});

test('CEF sends one login, clears passwords and handles delayed spawn and translated errors', async ({
    page,
}) => {
    await page.addInitScript(() => {
        const events: { name: string; payload: unknown }[] = [];

        Object.assign(window, {
            sentEvents: events,
            callEvent(name: string, payload: string) {
                events.push({ name, payload: JSON.parse(payload) });

                if (name === 'real-life:ready') {
                    setTimeout(() => {
                        window.dispatchEvent(
                            new CustomEvent('real-life:state', {
                                detail: {
                                    generation: 1,
                                    phase: 'auth',
                                    connected: true,
                                    pending: false,
                                },
                            }),
                        );
                    }, 0);
                }

                return true;
            },
        });
    });

    await page.goto('/');
    await expect(page.getByText('Interface preview')).toHaveCount(0);
    await page.getByLabel('Username', { exact: true }).fill('Angelo');
    await page.getByLabel('Password', { exact: true }).fill('my-long-password');
    await page.getByRole('button', { name: 'Return to the city' }).click();
    await expect(page.getByLabel('Password', { exact: true })).toHaveValue('');
    await expect(page.getByRole('button', { name: 'Checking your papers…' })).toBeDisabled();
    const sent = await page.evaluate(
        () =>
            (window as unknown as { sentEvents: { name: string; payload: unknown }[] }).sentEvents,
    );

    expect(sent.filter((event) => event.name === 'real-life:auth')).toEqual([
        {
            name: 'real-life:auth',
            payload: {
                mode: 'login',
                username: 'Angelo',
                password: 'my-long-password',
                locale: 'en',
                remember: false,
            },
        },
    ]);

    await page.evaluate(() =>
        window.dispatchEvent(
            new CustomEvent('real-life:state', {
                detail: {
                    generation: 1,
                    phase: 'auth',
                    connected: true,
                    pending: false,
                    error: 'invalidCredentials',
                },
            }),
        ),
    );

    await expect(page.getByRole('alert')).toHaveText('That username and password do not match.');
    await page.getByRole('button', { name: 'Čeština', exact: true }).click();
    await expect(page.getByRole('alert')).toHaveText('Uživatelské jméno nebo heslo není správné.');
    await page.evaluate(() =>
        window.dispatchEvent(
            new CustomEvent('real-life:state', {
                detail: {
                    generation: 1,
                    phase: 'spawning',
                    connected: true,
                    pending: true,
                    returning: true,
                },
            }),
        ),
    );

    await expect(page.getByRole('heading')).toHaveText('Město se otevírá…');
});

test('reduced motion is sent to the game camera', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await page.addInitScript(() => {
        Object.assign(window, {
            cameraPaused: false,
            callEvent(name: string, payload: string) {
                if (name === 'real-life:motion') {
                    Object.assign(window, { cameraPaused: JSON.parse(payload).reduced });
                }

                return true;
            },
        });
    });

    await page.goto('/');
    await expect
        .poll(() =>
            page.evaluate(() => (window as unknown as { cameraPaused: boolean }).cameraPaused),
        )
        .toBe(true);

    await page.getByRole('button', { name: 'Resume camera' }).click();
    await expect
        .poll(() =>
            page.evaluate(() => (window as unknown as { cameraPaused: boolean }).cameraPaused),
        )
        .toBe(false);
});

test('camera recorder accepts CEF key names when physical codes are empty', async ({ page }) => {
    await page.addInitScript(() => {
        const actions: Record<string, unknown>[] = [];

        Object.assign(window, {
            recorderActions: actions,
            callEvent(name: string, payload: string) {
                if (name === 'real-life:recorder') {
                    actions.push(JSON.parse(payload));
                }

                return true;
            },
        });
    });

    await page.goto('/');
    await page.evaluate(() =>
        window.dispatchEvent(
            new CustomEvent('real-life:state', {
                detail: {
                    generation: 1,
                    phase: 'auth',
                    connected: true,
                    pending: false,
                    cameraEditor: true,
                },
            }),
        ),
    );

    await expect(page.getByRole('button', { name: 'F4 · Camera recorder' })).toBeVisible();
    await page.evaluate(() =>
        window.dispatchEvent(new KeyboardEvent('keydown', { key: 'F4', code: '', bubbles: true })),
    );

    expect(
        await page.evaluate(
            () => (window as unknown as { recorderActions: unknown[] }).recorderActions,
        ),
    ).toContainEqual({ action: 'toggle' });

    await page.evaluate(() =>
        window.dispatchEvent(
            new CustomEvent('real-life:recorder-state', {
                detail: {
                    active: true,
                    replaying: false,
                    spline: 1,
                    points: 0,
                    total: 0,
                    seconds: 20,
                    dirty: false,
                    status: 'ready',
                    position: { x: 1, y: 2, z: 3 },
                    roll: 0,
                },
            }),
        ),
    );

    await expect(page.getByText('Camera recorder', { exact: true })).toBeVisible();
    await page.evaluate(() => {
        for (const key of ['F2', 'F3', 'F5', 'F6', 'w']) {
            window.dispatchEvent(new KeyboardEvent('keydown', { key, code: '', bubbles: true }));
        }
    });

    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    await page.keyboard.up('w');
    const actions = await page.evaluate(
        () => (window as unknown as { recorderActions: unknown[] }).recorderActions,
    );

    for (const action of ['point', 'new', 'play', 'save']) {
        expect(actions).toContainEqual({ action });
    }

    expect(actions).toContainEqual({ action: 'keys', keys: ['KeyW'] });
    expect(actions.at(-1)).toEqual({ action: 'keys', keys: [] });

    await page.evaluate(() => {
        const key = (type: string) =>
            window.dispatchEvent(new KeyboardEvent(type, { key: ' ', code: '', bubbles: true }));
        const move = (x: number, y: number, buttons = 0) =>
            window.dispatchEvent(new MouseEvent('mousemove', { clientX: x, clientY: y, buttons }));

        key('keydown');
        move(100, 100);
        move(125, 150);
        move(115, 160, 2);
        key('keyup');
        move(140, 170, 2);
        move(155, 175, 2);
        move(190, 180);
        key('keydown');
        window.dispatchEvent(new Event('blur'));
        move(200, 180);
        move(250, 180);
    });

    const mouseActions = await page.evaluate(
        () => (window as unknown as { recorderActions: Record<string, unknown>[] }).recorderActions,
    );

    expect(mouseActions.filter(({ action }) => action === 'roll' || action === 'look')).toEqual([
        { action: 'roll', x: 25 },
        { action: 'roll', x: -10 },
        { action: 'look', x: 15, y: 5 },
    ]);
});
