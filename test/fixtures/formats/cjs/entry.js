// CommonJS output. Written in the source's CJS spelling so the fixture is
// meaningful whether it is bundled as-is or converted from ESM.
const values = [1, 2, 3];

function double(n) {
    return n * 2;
}

const doubled = values.map(double);

exports.values = values;
exports.doubled = doubled;
module.exports = { values, doubled };
