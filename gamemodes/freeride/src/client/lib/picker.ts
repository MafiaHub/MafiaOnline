// The car picker (client/car_picker.ts) covers the screen while it is open;
// the HUD (client/hud.ts) closes its menu and hides behind it.
import { fields } from './payload';
import { events } from './runtime';

const PICKER_EVENT = 'fr:car:picker';

export function announcePicker(open: boolean): void {
    void events.emitLocal(PICKER_EVENT, { open });
}

export function onPicker(handler: (open: boolean) => void): void {
    events.onLocal(PICKER_EVENT, (data) => handler(Boolean(fields(data).open)));
}
