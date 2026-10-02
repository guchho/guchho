// IIFE output. No module syntax at all: the source hands work to the bundler
// the way an ordinary browser script does, and whatever produces it has to wrap
// it in a scope of its own.
(function () {
    "use strict";

    var names = ["ada", "grace", "alan"];
    var upper = names.map(function (name) {
        return name.toUpperCase();
    });

    console.log(upper.join(", "));
})();
