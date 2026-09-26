export type Permission =
    | 'car.spawn'
    | 'player.kick'
    | 'player.ban'
    | 'player.follow'
    | 'player.teleport'
    | 'weapon.give';

export const roles = {
    user: [],
    admin: [
        'car.spawn',
        'player.kick',
        'player.ban',
        'player.follow',
        'player.teleport',
        'weapon.give',
    ],
} as const satisfies Record<string, readonly Permission[]>;

export type Role = keyof typeof roles;

export function isRole(value: unknown): value is Role {
    return typeof value === 'string' && Object.hasOwn(roles, value);
}

export function can(role: Role, permission: Permission): boolean {
    return (roles[role] as readonly Permission[]).includes(permission);
}
