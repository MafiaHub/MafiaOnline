import en from '../shared/locales/en.json';
import cs from '../shared/locales/cs.json';
import type { Locale } from '../shared/protocol';

const dictionaries = { en, cs };

export type AdminMessage = keyof typeof en.admin;

export function adminText(
    locale: Locale,
    key: AdminMessage,
    values: Record<string, string | number> = {},
): string {
    return dictionaries[locale].admin[key].replace(/\{(\w+)\}/g, (placeholder, name: string) =>
        values[name] === undefined ? placeholder : String(values[name]),
    );
}
