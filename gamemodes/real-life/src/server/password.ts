import { randomBytes, scrypt, timingSafeEqual } from 'node:crypto';
import { Buffer } from 'node:buffer';

const cost = { N: 32768, r: 8, p: 1, maxmem: 64 * 1024 * 1024 };

function derive(password: string, salt: string): Promise<Buffer> {
    return new Promise((resolve, reject) => {
        scrypt(password, salt, 64, cost, (error, key) => (error ? reject(error) : resolve(key)));
    });
}

export async function hashPassword(password: string): Promise<string> {
    const salt = randomBytes(16).toString('hex');
    const key = await derive(password, salt);

    return `scrypt-v1:${salt}:${key.toString('hex')}`;
}

export async function verifyPassword(
    password: string,
    encoded: string | undefined,
): Promise<boolean> {
    // Missing accounts take the same expensive path as an incorrect password.
    const [version, salt, hash] = (
        encoded ?? `scrypt-v1:${'0'.repeat(32)}:${'0'.repeat(128)}`
    ).split(':');

    if (version !== 'scrypt-v1' || !/^[a-f0-9]{32}$/.test(salt) || !/^[a-f0-9]{128}$/.test(hash)) {
        return false;
    }

    const key = await derive(password, salt);

    return timingSafeEqual(key, Buffer.from(hash, 'hex')) && encoded !== undefined;
}
