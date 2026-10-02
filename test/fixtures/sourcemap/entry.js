// Source-map fixture. The value here is line structure: statements sit on
// clearly different lines inside nested function scopes, so a generated map has
// something to say that "one segment covering the whole file" would get wrong.

const DEFAULTS = {
    retries: 3,
    backoff: 100,
};

function normalise(options) {
    const merged = Object.assign({}, DEFAULTS, options);

    if (merged.retries < 0) {
        merged.retries = 0;
    }

    if (merged.backoff > 10000) {
        merged.backoff = 10000;
    }

    return merged;
}

function schedule(options) {
    const settings = normalise(options);
    const delays = [];

    for (let attempt = 0; attempt < settings.retries; attempt += 1) {
        delays.push(settings.backoff * Math.pow(2, attempt));
    }

    return delays;
}

function report(label, delays) {
    const total = delays.reduce(function (sum, delay) {
        return sum + delay;
    }, 0);

    console.log(label + ": " + total + "ms over " + delays.length + " attempts");
}

report("quick", schedule({ retries: 4 }));
report("slow", schedule({ retries: 6, backoff: 250 }));
