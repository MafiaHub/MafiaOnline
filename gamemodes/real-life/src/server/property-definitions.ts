import { existsSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { isPoint, isRecord } from '../shared/protocol';
import {
    DEFAULT_HOUSE_PRICE,
    type House,
    type Interior,
    type Entrance,
    type Pose,
    type Parking,
} from '../shared/properties';

const named = (value: unknown): value is string =>
    typeof value === 'string' && /^[a-z0-9_-]{1,32}$/.test(value);
const positiveId = (value: unknown): value is number =>
    typeof value === 'number' && Number.isInteger(value) && value > 0 && value < 1_000_000;
const pose = (value: unknown): value is Pose =>
    isRecord(value) &&
    isPoint(value.position) &&
    typeof value.heading === 'number' &&
    Number.isFinite(value.heading);

function parking(value: unknown): value is Parking {
    if (!isRecord(value) || !isPoint(value.position) || !isRecord(value.rotation)) {
        return false;
    }

    const rotation = value.rotation;

    return (
        ['w', 'x', 'y', 'z'].every(
            (k) => typeof rotation[k] === 'number' && Number.isFinite(rotation[k]),
        ) && Math.hypot(...['w', 'x', 'y', 'z'].map((k) => Number(rotation[k]))) > 0.001
    );
}

export class PropertyDefinitions {
    houses: House[];
    interiors: Interior[];
    entrances: Entrance[];
    constructor(private readonly directory: string) {
        this.houses = this.read<House>(
            'houses',
            (value) =>
                isRecord(value) &&
                positiveId(value.id) &&
                named(value.mission) &&
                Array.isArray(value.entries) &&
                value.entries.length > 0 &&
                value.entries.every(pose) &&
                Number.isSafeInteger(value.price) &&
                Number(value.price) > 0 &&
                Number(value.price) <= 1_000_000_000 &&
                (value.interior === null || named(value.interior)) &&
                Array.isArray(value.garages) &&
                value.garages.every(parking),
        );

        this.interiors = this.read<Interior>(
            'interiors',
            (value) =>
                isRecord(value) &&
                positiveId(value.id) &&
                named(value.name) &&
                named(value.mission) &&
                pose(value.exit),
        );

        this.entrances = this.read<Entrance>(
            'entrances',
            (value) =>
                isRecord(value) &&
                positiveId(value.id) &&
                named(value.interior) &&
                named(value.mission) &&
                pose(value.entry),
        );

        if (new Set(this.interiors.map((i) => i.name)).size !== this.interiors.length) {
            throw new Error('Duplicate interior names');
        }
    }

    private read<T extends { id: number }>(name: string, valid: (value: unknown) => boolean): T[] {
        const file = resolve(this.directory, `${name}.jsonl`);

        if (!existsSync(file)) {
            return [];
        }

        const records = readFileSync(file, 'utf8')
            .split('\n')
            .filter((line) => line.trim())
            .map((line) => JSON.parse(line) as T);

        if (
            records.length > 10000 ||
            records.some((value) => !valid(value)) ||
            new Set(records.map((value) => value.id)).size !== records.length
        ) {
            throw new Error(`Invalid property definitions: ${file}`);
        }

        return records;
    }

    private write<T>(name: string, records: T[]): void {
        const file = resolve(this.directory, `${name}.jsonl`);

        writeFileSync(
            `${file}.tmp`,
            records.map((value) => JSON.stringify(value)).join('\n') + '\n',
            { mode: 0o600 },
        );

        renameSync(`${file}.tmp`, file);
    }

    private next(records: { id: number }[]): number {
        // Houses are never removed, so ownership IDs remain stable.
        const id = Math.max(0, ...records.map((value) => value.id)) + 1;

        if (id >= 1_000_000) {
            throw new Error('Property ID limit reached');
        }

        return id;
    }

    addHouse(mission: string, entry: Pose): House {
        const house: House = {
            id: this.next(this.houses),
            mission,
            entries: [entry],
            price: DEFAULT_HOUSE_PRICE,
            interior: null,
            garages: [],
        };

        this.write('houses', [...this.houses, house]);
        this.houses.push(house);

        return house;
    }

    updateHouse(house: House): void {
        const records = this.houses.map((value) => (value.id === house.id ? house : value));

        this.write('houses', records);
        this.houses = records;
    }

    setInterior(name: string, mission: string, exit: Pose): Interior {
        if (!named(name)) {
            throw new Error('Invalid interior name');
        }

        const previous = this.interiors.find((value) => value.name === name);
        const interior = { id: previous?.id ?? this.next(this.interiors), name, mission, exit };
        const records = previous
            ? this.interiors.map((value) => (value.id === previous.id ? interior : value))
            : [...this.interiors, interior];

        this.write('interiors', records);
        this.interiors = records;

        return interior;
    }

    addEntrance(interior: Interior, entry: Pose): Entrance {
        const entrance = {
            id: this.next(this.entrances),
            interior: interior.name,
            mission: interior.mission,
            entry,
        };

        this.write('entrances', [...this.entrances, entrance]);
        this.entrances.push(entrance);

        return entrance;
    }

    removeEntrance(id: number): boolean {
        if (!this.entrances.some((value) => value.id === id)) {
            return false;
        }

        const records = this.entrances.filter((value) => value.id !== id);

        this.write('entrances', records);
        this.entrances = records;

        return true;
    }
}
