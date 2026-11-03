// One of the resource's HTML pages (src/client/*.html) in a web view. A page
// can take messages only once its document is ready (browserDocumentReady);
// until then emit drops them. Page events reach a handler only while the view
// that raised them is still the open one.
import { RESOURCE, events, log } from './runtime';

type ViewOptions = NonNullable<Parameters<typeof Web.createView>[1]>;

export class Page {
    private view: number | null = null;
    private ready = false;

    /**
     * @param name What the log calls it when it cannot open.
     * @param file The page under the resource's client folder.
     * @param onReady Runs each time a newly opened page's document is ready.
     */
    constructor(
        private readonly name: string,
        private readonly file: string,
        private readonly options: ViewOptions,
        onReady: () => void = () => {},
    ) {
        events.on('browserDocumentReady', ({ viewId }) => {
            if (this.view === null || viewId !== this.view) {
                return;
            }

            this.ready = true;
            onReady();
        });
    }

    get isOpen(): boolean {
        return this.view !== null;
    }

    get isReady(): boolean {
        return this.view !== null && this.ready;
    }

    /** Opens the view unless it is open; false (and logged) when it could not. */
    open(): boolean {
        if (this.view !== null) {
            return true;
        }

        try {
            this.view = Web.createView(
                `fw://resources/${RESOURCE}/client/${this.file}`,
                this.options,
            );
        } catch (error) {
            log.warn(`${this.name} could not open: ${String(error)}`);

            return false;
        }

        this.ready = false;

        return true;
    }

    /** Handles a page event of the view open now. */
    on(name: string, handler: (payload: unknown) => void): void {
        const view = this.view;

        if (view === null) {
            return;
        }

        Web.on(view, name, (payload) => {
            if (view === this.view) {
                handler(payload);
            }
        });
    }

    /** Sends to the page once it is ready; false (and dropped) before. */
    emit(name: string, payload?: unknown): boolean {
        if (this.view === null || !this.ready) {
            return false;
        }

        return payload === undefined
            ? Web.emit(this.view, name)
            : Web.emit(this.view, name, payload);
    }

    focus(focused: boolean): void {
        if (this.view !== null) {
            Web.focusView(this.view, focused);
        }
    }

    close(): void {
        if (this.view !== null) {
            Web.destroyView(this.view);
        }

        this.view = null;
        this.ready = false;
    }
}
