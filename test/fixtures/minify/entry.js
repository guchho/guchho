import { retryDelay } from "./dependency.js";

// neverCalledFromEntry is imported but never referenced on purpose: a build that
// keeps it has proved nothing, and one that drops it has proved dead-code
// elimination.
const attempts = [0, 1, 2, 3];

console.log(attempts.map(retryDelay));
