import { isRememberedAccount, type RememberedAccount } from '../shared/protocol';

const storageKey = (serverId: string) => `lhrp.account.${serverId}`;

export function loadRememberedAccount(serverId: string): RememberedAccount | null {
    try {
        const value: unknown = JSON.parse(localStorage.getItem(storageKey(serverId)) ?? 'null');

        if (
            isRememberedAccount(value) &&
            value.serverId === serverId &&
            value.expiresAt > Date.now()
        ) {
            return value;
        }

        localStorage.removeItem(storageKey(serverId));
    } catch {
        // Sign-in also works when CEF storage is unavailable.
    }

    return null;
}

export function saveRememberedAccount(account: RememberedAccount): void {
    try {
        localStorage.setItem(storageKey(account.serverId), JSON.stringify(account));
    } catch {
        // Storage is optional; completing this sign-in still takes priority.
    }
}

export function forgetRememberedAccount(serverId: string): void {
    try {
        localStorage.removeItem(storageKey(serverId));
    } catch {
        // A revoked server token cannot authenticate even if local removal fails.
    }
}
