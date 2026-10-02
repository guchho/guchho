// A dependency with something for every import spelling a bundler has to
// recognise: a named export, a default export, and a second named export that
// nothing imports, so dead-code elimination has something to drop.

export const multiplier = 3;

export function scale(value) {
    return value * multiplier;
}

export function describe(value) {
    return `${value} x ${multiplier}`;
}

export default { scale, describe };
