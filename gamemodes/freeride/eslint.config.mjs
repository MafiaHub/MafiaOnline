import stylistic from '@stylistic/eslint-plugin';
import tseslint from 'typescript-eslint';

export default [
    { ignores: ['node_modules/**', 'src/server/data/**'] },
    ...tseslint.configs.recommended.map((config) => ({
        ...config,
        files: ['src/**/*.ts', 'tests/*.ts'],
    })),
    {
        files: ['src/**/*.ts', 'tests/*.ts'],
        languageOptions: { parser: tseslint.parser },
        plugins: { '@stylistic': stylistic },
        rules: {
            curly: ['error', 'all'],
            eqeqeq: ['error', 'always'],
            'no-var': 'error',
            'prefer-const': 'error',
            '@typescript-eslint/ban-ts-comment': 'off',
            '@typescript-eslint/no-unused-vars': [
                'error',
                { argsIgnorePattern: '^_', varsIgnorePattern: '^_' },
            ],
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
