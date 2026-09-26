/** Callback and helper types referenced by the generated scripting API. */

/** Handler for a native or resource event. Async handlers may return a promise. */
export type EventHandler = (...args: unknown[]) => unknown | Promise<unknown>;

/** Removes an event subscription. */
export type Unsubscribe = () => void;

/** Sends a response to a resource request. */
export type MessageReply = (value: unknown) => void;

/** Receives a resource message and an optional reply callback. */
export type MessageHandler = (payload: unknown, reply: MessageReply) => void;

/** Receives a bound key and whether it was pressed or released. */
export type KeyHandler = (key: string, state: "down" | "up") => void;

/** Receives an event from a web view. */
export type WebEventHandler = (payload: unknown) => void;
