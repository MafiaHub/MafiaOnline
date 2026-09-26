import stylistic from '@stylistic/eslint-plugin';
import tseslint from 'typescript-eslint';

export default [
    { ignores: ['node_modules/**', 'test-results/**', 'playwright-report/**'] },
    {
        files: ['src/**/*.{ts,tsx}', 'tests/*.ts'],
        languageOptions: { parser: tseslint.parser },
        plugins: { '@stylistic': stylistic },
        rules: {
            curly: ['error', 'all'],
            '@stylistic/lines-between-class-members': [
                'error',
                'always',
                { exceptAfterSingleLine: true },
            ],
            '@stylistic/padding-line-between-statements': [
                'error',
                {
                    blankLine: 'always',
                    prev: ['block-like', 'if', 'multiline-expression'],
                    next: '*',
                },
                { blankLine: 'always', prev: '*', next: ['if', 'return', 'try'] },
                { blankLine: 'always', prev: ['const', 'let'], next: '*' },
                { blankLine: 'any', prev: ['const', 'let'], next: ['const', 'let'] },
            ],
        },
    },
];
