import { scale, describe } from "./dependency.js";
import helpers from "./dependency.js";

// Both import spellings reach the same module, which is the point: a bundler
// that resolves them separately emits it twice.
const numbers = [1, 2, 3];
const scaled = numbers.map(scale);

console.log(scaled);
console.log(describe(scaled.length));
console.log(helpers.scale(10));
