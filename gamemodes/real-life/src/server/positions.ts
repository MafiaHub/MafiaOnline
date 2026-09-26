import { appendFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { resolve } from 'node:path';
import type { Accounts } from './accounts';
import type { Session } from './session';
import { adminText, type AdminMessage } from './messages';
import { can } from '../shared/permissions';
import { EVENT, isPoint, isRecord, type Point } from '../shared/protocol';

export interface SavedPosition {
    comment: string;
    position: Point;
    rotation: { w: number; x: number; y: number; z: number };
    kind: 'player' | 'vehicle';
    model: string;
    mission: string;
    virtualWorld: number;
    account: string;
    savedAt: string;
}

/** One complete JSON object per line, with the comment inside the JSON. */
export function appendSavedPosition(file: string, position: SavedPosition): void {
    appendFileSync(file, JSON.stringify(position) + '\n', { mode: 0o600 });
}

interface Capture {
    request: string;
    comment: string;
    generation: number;
    world: number;
    accountId: number;
    expires: number;
}

export class Positions {
    private readonly pending = new Map<number, Capture>();
    private readonly last = new Map<number, number>();
    readonly file: string;

    constructor(
        directory: string,
        private readonly accounts: Accounts,
        private readonly sessions: Map<number, Session>,
    ) {
        this.file = resolve(directory, 'saved-positions.jsonl');
    }

    private reply(session: Session, key: AdminMessage, values: Record<string, string> = {}): void {
        session.player.sendMessage(adminText(session.locale, key, values), 0xc8b382);
    }

    private authorized(session: Session): boolean {
        return Boolean(
            session.account &&
            session.entered &&
            can(this.accounts.find(session.account.username)?.role ?? 'user', 'position.save') &&
            !this.accounts.getBan(session.account.id),
        );
    }

    handle(player: Player, command: string, args: string[]): boolean {
        if (command.toLowerCase() !== 'savepos') {
            return false;
        }

        const session = this.sessions.get(player.id);

        if (!session?.account || !session.entered) {
            player.sendMessage(adminText(session?.locale ?? 'en', 'loginRequired'));

            return true;
        }

        if (!this.authorized(session)) {
            this.reply(session, 'denied');

            return true;
        }

        if (!World.isReady() || !player.spawned || !player.alive) {
            this.reply(session, 'spawnRequired');

            return true;
        }

        const comment = args.join(' ').trim();

        if (
            !comment ||
            comment.length > 160 ||
            Buffer.byteLength(comment) > 240 ||
            /[\x00-\x1f\x7f]/.test(comment)
        ) {
            this.reply(session, 'positionUsage');

            return true;
        }

        const now = Date.now();

        if (now - (this.last.get(player.id) ?? 0) < 1000 || this.pending.has(player.id)) {
            this.reply(session, 'cooldown');

            return true;
        }

        const capture: Capture = {
            request: randomUUID(),
            comment,
            generation: World.getMissionGeneration(),
            world: player.virtualWorld,
            accountId: session.account.id,
            expires: now + 10000,
        };

        this.last.set(player.id, now);
        this.pending.set(player.id, capture);
        player.emit(
            EVENT.positionCapture,
            JSON.stringify({
                request: capture.request,
                generation: capture.generation,
                world: capture.world,
            }),
        );

        return true;
    }

    captured(player: Player, payload: unknown): void {
        const session = this.sessions.get(player.id);
        const capture = this.pending.get(player.id);

        if (!session || !capture || !isRecord(payload) || payload.request !== capture.request) {
            return;
        }

        this.pending.delete(player.id);

        if (!this.authorized(session) || session.account?.id !== capture.accountId) {
            return;
        }

        const vehicle = player.getVehicle();
        const transform = payload.transform;
        const rotation = isRecord(transform) ? transform.rotation : undefined;
        const validRotation =
            isRecord(rotation) &&
            ['w', 'x', 'y', 'z'].every(
                (k) => typeof rotation[k] === 'number' && Number.isFinite(rotation[k]),
            );
        const length = validRotation
            ? Math.hypot(...['w', 'x', 'y', 'z'].map((k) => Number(rotation[k])))
            : 0;

        if (
            Date.now() > capture.expires ||
            !player.spawned ||
            !player.alive ||
            payload.generation !== capture.generation ||
            capture.generation !== World.getMissionGeneration() ||
            payload.world !== capture.world ||
            capture.world !== player.virtualWorld ||
            payload.vehicleId !== (vehicle?.id ?? null) ||
            !isRecord(transform) ||
            !isPoint(transform.position) ||
            !validRotation ||
            Math.abs(length - 1) > 0.01
        ) {
            this.reply(session, 'positionUnavailable');

            return;
        }

        try {
            appendSavedPosition(this.file, {
                comment: capture.comment,
                position: transform.position,
                rotation: rotation as SavedPosition['rotation'],
                kind: vehicle ? 'vehicle' : 'player',
                model: (vehicle ?? player).model,
                mission: World.getMission(),
                virtualWorld: player.virtualWorld,
                account: session.account!.username,
                savedAt: new Date().toISOString(),
            });

            this.reply(session, 'positionSaved', { comment: capture.comment });
        } catch (error) {
            process.stderr.write(`[lhrp] Could not save position: ${error}\n`);
            this.reply(session, 'failed');
        }
    }

    update(): void {
        for (const [id, capture] of this.pending) {
            if (Date.now() > capture.expires) {
                this.pending.delete(id);
                const session = this.sessions.get(id);

                if (session && this.authorized(session)) {
                    this.reply(session, 'positionUnavailable');
                }
            }
        }
    }

    release(player: Player): void {
        this.pending.delete(player.id);
        this.last.delete(player.id);
    }

    reset(): void {
        this.pending.clear();
        this.last.clear();
    }
}
