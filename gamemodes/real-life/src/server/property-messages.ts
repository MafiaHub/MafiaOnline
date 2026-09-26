import en from '../shared/locales/en.json';
import cs from '../shared/locales/cs.json';
import type { Locale } from '../shared/protocol';
export type PropertyMessage = keyof typeof en.properties;
export function propertyText(
    locale: Locale,
    key: PropertyMessage,
    values: Record<string, string | number> = {},
): string {
    return { en, cs }[locale].properties[key].replace(/\{(\w+)\}/g, (placeholder, name: string) =>
        values[name] === undefined ? placeholder : String(values[name]),
    );
}
