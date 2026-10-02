// Long names and slack whitespace on purpose: minification has to shorten and
// reflow this. The unused export below is the half of the fixture that dead-code
// elimination has to remove.
const MAXIMUM_ALLOWED_RETRY_ATTEMPTS = 3;
const RECOVERY_BACKOFF_MILLISECONDS = 250;

export function retryDelay(attemptNumber) {
    if (attemptNumber < MAXIMUM_ALLOWED_RETRY_ATTEMPTS) {
        return RECOVERY_BACKOFF_MILLISECONDS * (attemptNumber + 1);
    }
    return 0;
}

export function neverCalledFromEntry() {
    return "unused, and long enough to show in output if elimination fails";
}
