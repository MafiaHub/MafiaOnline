// The car picker (client/car_picker.ts) covers the screen while it is open;
// the HUD (client/hud.ts) closes its menu and hides behind it. Its preview is
// native, seen through a hole in the page, so world labels drawn natively
// would show over the car: their drawers ask isPickerOpen() each frame.
import { fields } from './payload';
import { events } from './runtime';

const PICKER_EVENT = 'fr:car:picker';

let pickerOpen = false;

export function announcePicker(open: boolean): void {
    pickerOpen = open;
    void events.emitLocal(PICKER_EVENT, { open });
}

export function isPickerOpen(): boolean {
    return pickerOpen;
}

export function onPicker(handler: (open: boolean) => void): void {
    events.onLocal(PICKER_EVENT, (data) => handler(Boolean(fields(data).open)));
}
