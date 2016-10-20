import LowLevelBinary from '../LowLevelBinary.js';

const values = [
    "",
    "0",
    "Hello, World!",
    "Il dit non avec la tête, mais il dit oui avec le cœur",
    "焼きたて!! ジャぱん",
    "(╯°□°）╯︵ ┻━┻",
    "�",
    // Should we use code points instead of UTF-16?
    //        The following doesn't work: "👨‍❤️‍💋‍👨",
];

for (const i of values) {
    let b = new LowLevelBinary();
    b.string(i);
    const v = b.getString(0);
    if (v !== i)
        throw new Error(`Wrote "${i}" and read back "${v}"`);
}
