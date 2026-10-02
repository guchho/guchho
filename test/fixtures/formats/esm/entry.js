// ES module output. The top-level await is the interesting part: it is legal
// here and illegal under CommonJS, so a format conversion that is supposed to
// preserve top-level await has something to preserve.
const settings = await Promise.resolve({
    retries: 2,
    timeout: 5000,
});

export function describeSettings() {
    return `retries=${settings.retries} timeout=${settings.timeout}`;
}

export default { describeSettings };
