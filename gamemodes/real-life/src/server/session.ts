import type { Account } from './accounts';
import type { Locale, RememberedAccount } from '../shared/protocol';

export interface Session {
    player: Player;
    account?: Account;
    locale: Locale;
    busy: boolean;
    entered: boolean;
    announced?: boolean;
    respawnAt: number;
    rememberUntil: number;
    remembered?: RememberedAccount;
    rememberToken?: string;
    forgetRemembered?: boolean;
}
