// UMD output. The three-branch preamble is the whole shape of the format: AMD
// takes the define branch, CommonJS takes the exports branch, and a browser
// with neither falls through to the global. Whichever branch is taken, the
// inner body has to be identical.
(function (root, factory) {
    if (typeof define === "function" && define.amd) {
        define([], factory);
    } else if (typeof module === "object" && module.exports) {
        module.exports = factory();
    } else {
        root.GuchhoFixture = factory();
    }
})(typeof self !== "undefined" ? self : this, function () {
    "use strict";

    var limits = { low: 1, high: 10 };

    function clamp(n) {
        return Math.min(limits.high, Math.max(limits.low, n));
    }

    return { limits: limits, clamp: clamp };
});
